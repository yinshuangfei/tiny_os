/*
 * SysV 共享内存（对齐 Linux ipc/shm 教学子集）。
 *
 * shmget → 全局段表 + 预分配清零物理页
 * shmat  → 登记 VMA，立即映射同一组物理页（MAP_SHARED）
 * shmdt / munmap / exit / exec → 减 nattch；IPC_RMID 且 nattch==0 时释放
 * fork  → uvmcopy 会把可写页标成 COW，随后 shm_fork_fix 恢复共享可写
 */
#include "../types.h"
#include "../defs.h"
#include "../param.h"
#include "../proc.h"
#include "../syscall.h"
#include "../lock/spinlock.h"
#include "../mm/memlayout.h"
#include "../mm/mmu.h"
#include "../mm/vm.h"
#include "shm.h"

#define NSHM		32
#define SHM_MAX_PAGES	256	/* 单段上限 1MiB */

struct shm_seg {
	int inuse;		/* 是否被使用 */
	int key;		/* 共享内存的键值 */
	uint size;		/* 页对齐后长度 */
	uint npages;		/* 页数 */
	void *pages[SHM_MAX_PAGES];	/* 物理页指针数组 */
	int nattch;		/* 挂载次数 */
	int dest;		/* destroy 标记 */
};

static struct spinlock shm_lock;
static struct shm_seg shm_table[NSHM];
static int shm_inited;

static void shm_init(void)
{
	if (shm_inited)
		return;
	initlock(&shm_lock, "shm");
	shm_inited = 1;
}

static struct shm_seg *shm_get(int shmid)
{
	if (shmid < 0 || shmid >= NSHM)
		return 0;
	if (!shm_table[shmid].inuse)
		return 0;
	return &shm_table[shmid];
}

/* 释放所有物理页 */
static void shm_free_pages(struct shm_seg *s)
{
	uint i;

	for (i = 0; i < s->npages; i++) {
		if (s->pages[i]) {
			put_page(s->pages[i]);
			s->pages[i] = 0;
		}
	}
	s->npages = 0;
	s->size = 0;
}

static void shm_try_destroy_locked(struct shm_seg *s)
{
	if (!s->inuse || !s->dest || s->nattch != 0)
		return;
	shm_free_pages(s);
	s->inuse = 0;
	s->key = 0;
	s->dest = 0;
}

static int shm_map_range(struct proc *p, struct vma *v, struct shm_seg *s)
{
	uint i, va;
	int perm;

	perm = PTE_P;
	if (v->prot & PROT_WRITE)
		perm |= PTE_W;

	for (i = 0; i < s->npages; i++) {
		va = v->start + i * PGSIZE;
		if (walkaddr(p->pagetable, va) != 0)
			(void)uvmunmap(p->pagetable, va, 1, 1);
		get_page(s->pages[i]);
		if (uvmmap(p->pagetable, va, (uint)s->pages[i], PGSIZE, perm) < 0) {
			put_page(s->pages[i]);
			return -1;
		}
	}
	return 0;
}

static void shm_unmap_range(struct proc *p, struct vma *v)
{
	uint va;

	for (va = v->start; va < v->end; va += PGSIZE)
		(void)uvmunmap(p->pagetable, va, 1, 1);
}

/* 创建或查找共享内存段 */
int do_shmget(int key, uint size, int shmflg)
{
	struct shm_seg *s;
	int i, id;
	uint npages, j;
	void *mem;

	shm_init();
	acquire(&shm_lock);

	if (key != IPC_PRIVATE) {
		for (i = 0; i < NSHM; i++) {
			s = &shm_table[i];
			if (!s->inuse || s->dest || s->key != key)
				continue;
			if (size > s->size) {
				release(&shm_lock);
				return -1;
			}
			if ((shmflg & IPC_CREAT) && (shmflg & IPC_EXCL)) {
				release(&shm_lock);
				return -1;
			}
			release(&shm_lock);
			return i;
		}
		if (!(shmflg & IPC_CREAT)) {
			release(&shm_lock);
			return -1;
		}
	}

	if (size == 0) {
		release(&shm_lock);
		return -1;
	}
	size = PGROUNDUP(size);
	npages = size / PGSIZE;
	if (npages == 0 || npages > SHM_MAX_PAGES) {
		release(&shm_lock);
		return -1;
	}

	id = -1;
	for (i = 0; i < NSHM; i++) {
		if (!shm_table[i].inuse) {
			id = i;
			break;
		}
	}
	if (id < 0) {
		release(&shm_lock);
		return -1;
	}

	s = &shm_table[id];
	memset(s, 0, sizeof(*s));
	s->inuse = 1;
	s->key = key;
	s->size = size;
	s->npages = npages;
	s->nattch = 0;
	s->dest = 0;

	for (j = 0; j < npages; j++) {
		mem = alloc_page();
		if (!mem) {
			s->npages = j;
			shm_free_pages(s);
			s->inuse = 0;
			release(&shm_lock);
			return -1;
		}
		memset(mem, 0, PGSIZE);
		s->pages[j] = mem;
	}

	release(&shm_lock);
	return id;
}

/* 附着共享内存段 */
uint do_shmat(int shmid, uint shmaddr, int shmflg)
{
	struct proc *p = myproc();
	struct shm_seg *s;
	struct vma *v;
	uint start, len;
	int prot;

	if (!p || !p->pagetable)
		return (uint)-1;

	shm_init();
	acquire(&shm_lock);
	s = shm_get(shmid);
	if (!s || s->dest) {
		release(&shm_lock);
		return (uint)-1;
	}
	len = s->size;
	release(&shm_lock);

	prot = PROT_READ;
	if (!(shmflg & SHM_RDONLY))
		prot |= PROT_WRITE;

	if (shmaddr != 0) {
		if (shmaddr & (PGSIZE - 1))
			return (uint)-1;
		start = shmaddr;
		if (!mmap_region_ok(p, start, start + len))
			return (uint)-1;
	} else {
		start = mmap_find_addr(p, len);
		if (start == 0)
			return (uint)-1;
	}

	v = vma_create(p, start, start + len, prot, MAP_SHARED, shmid);
	if (!v)
		return (uint)-1;

	acquire(&shm_lock);
	s = shm_get(shmid);
	if (!s || s->dest) {
		release(&shm_lock);
		v->used = 0;
		return (uint)-1;
	}
	if (shm_map_range(p, v, s) < 0) {
		release(&shm_lock);
		v->used = 0;
		return (uint)-1;
	}
	s->nattch++;
	release(&shm_lock);
	return start;
}

/* 卸载共享内存段 */
int shm_munmap_vma(struct proc *p, struct vma *v)
{
	struct shm_seg *s;
	int shmid;

	if (!p || !v || !v->used)
		return -1;
	shmid = vma_shmid(v);
	if (shmid < 0)
		return -1;

	shm_unmap_range(p, v);
	v->used = 0;
	v->flags = 0;

	shm_init();
	acquire(&shm_lock);
	s = shm_get(shmid);
	if (s) {
		if (s->nattch > 0)
			s->nattch--;
		shm_try_destroy_locked(s);
	}
	release(&shm_lock);
	return 0;
}

/* 卸载共享内存段 */
int do_shmdt(uint shmaddr)
{
	struct proc *p = myproc();
	struct vma *v;
	int i;

	if (!p || !p->pagetable)
		return -1;
	if (shmaddr & (PGSIZE - 1))
		return -1;

	for (i = 0; i < NVMA; i++) {
		v = &p->vmas[i];
		if (!v->used || vma_shmid(v) < 0)
			continue;
		if (v->start == shmaddr)
			return shm_munmap_vma(p, v);
	}
	return -1;
}

/* 控制共享内存段 */
int do_shmctl(int shmid, int cmd, uint buf)
{
	struct shm_seg *s;

	(void)buf;
	if (cmd != IPC_RMID)
		return -1;

	shm_init();
	acquire(&shm_lock);
	s = shm_get(shmid);
	if (!s) {
		release(&shm_lock);
		return -1;
	}
	s->dest = 1;
	shm_try_destroy_locked(s);
	release(&shm_lock);
	return 0;
}

/* 处理 fork 时的共享内存段修复 */
void shm_fork_fix(struct proc *child, struct proc *parent)
{
	struct vma *v;
	struct shm_seg *s;
	int i, shmid;

	if (!child || !parent)
		return;

	shm_init();
	for (i = 0; i < NVMA; i++) {
		v = &child->vmas[i];
		if (!v->used)
			continue;
		shmid = vma_shmid(v);
		if (shmid < 0)
			continue;

		acquire(&shm_lock);
		s = shm_get(shmid);
		if (!s || s->dest) {
			release(&shm_lock);
			/* 段已消失：卸掉子进程映射，避免悬空 */
			shm_unmap_range(child, v);
			v->used = 0;
			v->flags = 0;
			continue;
		}
		/* 父子均从 COW 恢复为共享可写（或只读） */
		if (shm_map_range(parent, &parent->vmas[i], s) < 0 ||
		    shm_map_range(child, v, s) < 0) {
			release(&shm_lock);
			continue;
		}
		s->nattch++;
		release(&shm_lock);
	}
}

/* 卸载所有共享内存段 */
void shm_detach_all(struct proc *p)
{
	struct vma *v;
	int i;

	if (!p)
		return;
	for (i = 0; i < NVMA; i++) {
		v = &p->vmas[i];
		if (!v->used || vma_shmid(v) < 0)
			continue;
		(void)shm_munmap_vma(p, v);
	}
}

/* 共享内存页缺页处理 */
int shm_demand_fault(struct proc *p, struct vma *v, uint page, int write)
{
	struct shm_seg *s;
	uint idx;
	int perm;
	int shmid;

	if (!p || !v)
		return -1;
	shmid = vma_shmid(v);
	if (shmid < 0)
		return -1;
	if (write && !(v->prot & PROT_WRITE))
		return -1;
	if (page < v->start || page >= v->end)
		return -1;

	idx = (page - v->start) / PGSIZE;
	shm_init();
	acquire(&shm_lock);
	s = shm_get(shmid);
	if (!s || idx >= s->npages || !s->pages[idx]) {
		release(&shm_lock);
		return -1;
	}
	perm = PTE_P;
	if (v->prot & PROT_WRITE)
		perm |= PTE_W;
	get_page(s->pages[idx]);
	if (uvmmap(p->pagetable, page, (uint)s->pages[idx], PGSIZE, perm) < 0) {
		put_page(s->pages[idx]);
		release(&shm_lock);
		return -1;
	}
	release(&shm_lock);
	return 0;
}

int sys_shmget(struct trapframe *tf)
{
	int key, shmflg;
	uint size;

	if (argint(tf, 0, &key) < 0)
		return -1;
	if (argaddr(tf, 1, &size) < 0)
		return -1;
	if (argint(tf, 2, &shmflg) < 0)
		return -1;
	return do_shmget(key, size, shmflg);
}

int sys_shmat(struct trapframe *tf)
{
	int shmid, shmflg;
	uint shmaddr;

	if (argint(tf, 0, &shmid) < 0)
		return -1;
	if (argaddr(tf, 1, &shmaddr) < 0)
		return -1;
	if (argint(tf, 2, &shmflg) < 0)
		return -1;
	return (int)do_shmat(shmid, shmaddr, shmflg);
}

int sys_shmdt(struct trapframe *tf)
{
	uint shmaddr;

	if (argaddr(tf, 0, &shmaddr) < 0)
		return -1;
	return do_shmdt(shmaddr);
}

int sys_shmctl(struct trapframe *tf)
{
	int shmid, cmd;
	uint buf;

	if (argint(tf, 0, &shmid) < 0)
		return -1;
	if (argint(tf, 1, &cmd) < 0)
		return -1;
	if (argaddr(tf, 2, &buf) < 0)
		return -1;
	return do_shmctl(shmid, cmd, buf);
}
