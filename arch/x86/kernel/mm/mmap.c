/*
 * 匿名 mmap / munmap（对齐 Linux mmap2 教学子集）。
 * MAP_ANONYMOUS|MAP_PRIVATE；按需分页（只建 VMA，物理页在 #PF 时填零）。
 * SysV shmat 复用找址 / VMA 槽；整段 munmap 等价 shmdt。
 */
#include "../types.h"
#include "../defs.h"
#include "../param.h"
#include "../proc.h"
#include "../ipc/shm.h"
#include "memlayout.h"
#include "mmu.h"
#include "vm.h"

/* 判断 [start,end) 是否与已有 VMA 冲突 */
static int vma_overlap_range(struct proc *p, uint start, uint end)
{
	int i;

	for (i = 0; i < NVMA; i++) {
		if (!p->vmas[i].used)
			continue;
		if (start < p->vmas[i].end && end > p->vmas[i].start)
			return 1;
	}
	return 0;
}

/* [start,end) 是否与已有 VMA / 堆 / 已映射页冲突 */
static int region_available(struct proc *p, uint start, uint end)
{
	uint va;

	if (start < USERBASE || end > USERHEAP_TOP || start >= end)
		return 0;
	if (start < PGROUNDUP(p->brk))
		return 0;
	if (vma_overlap_range(p, start, end))
		return 0;
	for (va = start; va < end; va += PGSIZE) {
		if (walkaddr(p->pagetable, va) != 0)
			return 0;
	}
	return 1;
}

/* 查找可用的映射地址（自 USERHEAP_TOP 向下） */
static uint find_mmap_addr(struct proc *p, uint len)
{
	uint low, high, start;

	low = PGROUNDUP(p->brk);
	high = USERHEAP_TOP;
	if (len == 0 || low >= high || len > high - low)
		return 0;

	start = PGROUNDDOWN(high - len);
	for (;;) {
		if (start < low)
			return 0;
		if (region_available(p, start, start + len))
			return start;
		if (start < low + PGSIZE)
			return 0;
		start -= PGSIZE;
	}
}

static int prot_ok(int prot)
{
	return (prot & (PROT_READ | PROT_WRITE | PROT_EXEC)) != 0;
}

static struct vma *vma_alloc_slot(struct proc *p)
{
	int i;

	for (i = 0; i < NVMA; i++) {
		if (!p->vmas[i].used)
			return &p->vmas[i];
	}
	return 0;
}

static struct vma *vma_find(struct proc *p, uint start, uint end)
{
	int i;

	for (i = 0; i < NVMA; i++) {
		if (!p->vmas[i].used)
			continue;
		if (p->vmas[i].start == start && p->vmas[i].end == end)
			return &p->vmas[i];
	}
	return 0;
}

/* 按虚址查找包含该地址的 VMA（供 #PF / demand fault） */
struct vma *vma_lookup(struct proc *p, uint va)
{
	int i;

	if (!p)
		return 0;
	for (i = 0; i < NVMA; i++) {
		if (!p->vmas[i].used)
			continue;
		if (va >= p->vmas[i].start && va < p->vmas[i].end)
			return &p->vmas[i];
	}
	return 0;
}

void vma_clear(struct proc *p)
{
	int i;

	if (!p)
		return;
	for (i = 0; i < NVMA; i++)
		p->vmas[i].used = 0;
}

void vma_copy(struct proc *dst, struct proc *src)
{
	int i;

	if (!dst || !src)
		return;
	for (i = 0; i < NVMA; i++)
		dst->vmas[i] = src->vmas[i];
}

uint mmap_find_addr(struct proc *p, uint len)
{
	return find_mmap_addr(p, len);
}

int mmap_region_ok(struct proc *p, uint start, uint end)
{
	return region_available(p, start, end);
}

struct vma *vma_create(struct proc *p, uint start, uint end,
		       int prot, int flags, int shmid)
{
	struct vma *v;

	if (!p || start >= end)
		return 0;
	if (!region_available(p, start, end))
		return 0;
	v = vma_alloc_slot(p);
	if (!v)
		return 0;
	v->used = 1;
	v->start = start;
	v->end = end;
	v->prot = prot;
	v->flags = vma_flags_with_shmid(flags, shmid);
	return v;
}

int vma_overlaps_brk(struct proc *p, uint old_brk, uint new_brk)
{
	if (!p || new_brk <= old_brk)
		return 0;
	return vma_overlap_range(p, old_brk, new_brk);
}

static void mmap_unmap_pages(struct proc *p, uint start, uint end)
{
	uint va;

	for (va = start; va < end; va += PGSIZE)
		(void)uvmunmap(p->pagetable, va, 1, 1);
}

/*
 * 匿名映射：成功返回用户 VA；失败返回 (uint)-1。
 * 按需分页：只登记 VMA，不立即分配物理页。
 */
uint do_mmap(struct proc *p, uint addr, uint len, int prot, int flags,
	     int fd, uint pgoff)
{
	struct vma *v;
	uint start;

	(void)pgoff;
	if (!p || !p->pagetable || len == 0)
		return (uint)-1;

	len = PGROUNDUP(len);
	if (len == 0 || len > USERHEAP_TOP - USERBASE)
		return (uint)-1;

	flags &= 0xffff;
	if (!(flags & MAP_ANONYMOUS) || !(flags & MAP_PRIVATE))
		return (uint)-1;
	if (flags & MAP_SHARED)
		return (uint)-1;
	if (fd != -1 && fd != 0xffffffff)
		return (uint)-1;

	if (!prot_ok(prot))
		return (uint)-1;

	if (flags & MAP_FIXED) {
		if (addr & (PGSIZE - 1))
			return (uint)-1;
		start = addr;
		if (!region_available(p, start, start + len))
			return (uint)-1;
	} else {
		start = find_mmap_addr(p, len);
		if (start == 0)
			return (uint)-1;
	}

	v = vma_alloc_slot(p);
	if (!v)
		return (uint)-1;

	v->used = 1;
	v->start = start;
	v->end = start + len;
	v->prot = prot;
	v->flags = flags;	/* 无 shm 编码 */
	return start;
}

int do_munmap(struct proc *p, uint addr, uint len)
{
	struct vma *v;
	uint end;

	if (!p || !p->pagetable || len == 0)
		return -1;
	if (addr & (PGSIZE - 1))
		return -1;

	len = PGROUNDUP(len);
	end = addr + len;
	if (end < addr)
		return -1;

	/* 教学版：只允许整段卸映射 */
	v = vma_find(p, addr, end);
	if (!v)
		return -1;

	if (vma_shmid(v) >= 0)
		return shm_munmap_vma(p, v);

	mmap_unmap_pages(p, v->start, v->end);
	v->used = 0;
	return 0;
}
