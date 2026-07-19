/*
 * 匿名 mmap / munmap（对齐 Linux mmap2 教学子集）。
 * 仅支持 MAP_ANONYMOUS|MAP_PRIVATE；eager 分配清零页（无 demand paging）。
 */
#include "../types.h"
#include "../defs.h"
#include "../param.h"
#include "../proc.h"
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
		/* 检查页表项是否存在, 如果存在, 则说明该地址已经被映射, 不能再次映射 */
		if (walkaddr(p->pagetable, va) != 0)
			return 0;
	}
	/* 如果所有检查都通过, 则说明该地址可以被映射 */
	return 1;
}

/* 查找可用的映射地址 */
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

/* 将保护标志转换为权限标志 */
static int prot_to_perm(int prot)
{
	int perm;

	if (!(prot & (PROT_READ | PROT_WRITE | PROT_EXEC)))
		return -1;
	perm = PTE_P;
	if (prot & PROT_WRITE)
		perm |= PTE_W;
	return perm;
}

/* 分配 VMA 区域表槽位 */
static struct vma *vma_alloc_slot(struct proc *p)
{
	int i;

	for (i = 0; i < NVMA; i++) {
		if (!p->vmas[i].used)
			return &p->vmas[i];
	}
	return 0;
}

/* 查找 VMA 区域表槽位 */
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

/* 清空 VMA 区域表 */
void vma_clear(struct proc *p)
{
	int i;

	if (!p)
		return;
	for (i = 0; i < NVMA; i++)
		p->vmas[i].used = 0;
}

/* 复制 VMA 区域表 */
void vma_copy(struct proc *dst, struct proc *src)
{
	int i;

	if (!dst || !src)
		return;
	for (i = 0; i < NVMA; i++)
		dst->vmas[i] = src->vmas[i];
}

/* 检查 VMA 区域表是否与堆冲突 */
int vma_overlaps_brk(struct proc *p, uint old_brk, uint new_brk)
{
	if (!p || new_brk <= old_brk)
		return 0;
	return vma_overlap_range(p, old_brk, new_brk);
}

/* 解除映射的页 */
static void mmap_unmap_pages(struct proc *p, uint start, uint end)
{
	uint va;

	for (va = start; va < end; va += PGSIZE)
		(void)uvmunmap(p->pagetable, va, 1, 1);
}

/*
 * 匿名映射：成功返回用户 VA；失败返回 (uint)-1。
 * pgoff 忽略（匿名）。
 */
uint do_mmap(struct proc *p, uint addr, uint len, int prot, int flags,
	     int fd, uint pgoff)
{
	struct vma *v;
	uint start, a;
	int perm;
	char *mem;

	(void)pgoff;
	if (!p || !p->pagetable || len == 0)
		return (uint)-1;

	len = PGROUNDUP(len);
	if (len == 0 || len > USERHEAP_TOP - USERBASE)
		return (uint)-1;

	if (!(flags & MAP_ANONYMOUS) || !(flags & MAP_PRIVATE))
		return (uint)-1;
	if (flags & MAP_SHARED)
		return (uint)-1;
	if (fd != -1 && fd != 0xffffffff)
		return (uint)-1;

	perm = prot_to_perm(prot);
	if (perm < 0)
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

	for (a = start; a < start + len; a += PGSIZE) {
		mem = alloc_page();
		if (!mem)
			goto bad;
		memset(mem, 0, PGSIZE);
		if (uvmmap(p->pagetable, a, (uint)mem, PGSIZE, perm) < 0) {
			free_page(mem);
			goto bad;
		}
	}

	v->used = 1;
	v->start = start;
	v->end = start + len;
	v->prot = prot;
	v->flags = flags;
	return start;

bad:
	mmap_unmap_pages(p, start, a);
	return (uint)-1;
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

	/* 教学版：只允许整段卸映射（与创建时起止一致） */
	v = vma_find(p, addr, end);
	if (!v)
		return -1;

	mmap_unmap_pages(p, v->start, v->end);
	v->used = 0;
	return 0;
}
