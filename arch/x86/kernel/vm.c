/**
 * x86 32 位两级页表里，PDE 和 PTE 格式相同，都是 32 位（4 字节）。
 * 位编号：31 为最高位，0 为最低位（与 Intel SDM 一致）。
 *  31                    12 11  9 8 7 6 5 4 3 2 1 0
 * ┌────────────────────────┬─────┬─┬─┬─┬─┬─┬─┬─┬─┬─┐
 * │   Physical Base        │ AVL │G│P│D│A│P│P│U│R│P│
 * │   (PPN, 20 bits)       │ 3b  │ │S│ │ │C│W│/│/│ │
 * │                        │     │ │ │ │ │D│T│S│w│ │
 * └────────────────────────┴─────┴─┴─┴─┴─┴─┴─┴─┴─┴─┘
 */

#include "types.h"
#include "defs.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"

/* 内核页目录, 所有内核线程共享。和 Linux 一样：内核页表是全局共用的 */
pagetable_t kernel_pgdir;

/* -------------------------------------------------------------------------- */
/* 页表遍历与映射（内核 / 用户页表共用）                                         */
/* -------------------------------------------------------------------------- */

/*
 * Return the address of the PTE for virtual address va.
 * Create intermediate page-table pages when alloc != 0.
 */
static pte_t *walk(pagetable_t pgdir, uint va, int alloc, int user)
{
	pde_t *pde = &pgdir[PDX(va)];
	pagetable_t pt;

	if (*pde & PTE_P) {
		pt = (pagetable_t)PTE_ADDR(*pde);
	} else {
		if (!alloc)
			return 0;
		pt = (pagetable_t)alloc_page();
		if (pt == 0)
			return 0;
		/*
		 * 用户页表中的 PDE 须带 PTE_U，否则 ring3 访问该页表下任何页
		 * 都会 #PF（err 常见为 0x5 保护违规）。
		 */
		if (user)
			*pde = PA2PTE((uint)pt) | PTE_P | PTE_W | PTE_U;
		else
			*pde = PA2PTE((uint)pt) | PTE_P | PTE_W;
	}
	return &pt[PTX(va)];
}

static int mappages(pagetable_t pgdir, uint va, uint size, uint pa, int perm,
		    int user)
{
	pte_t *pte;
	uint a, last;

	if ((va % PGSIZE) != 0 || (pa % PGSIZE) != 0) {
		printf("page not aligned: va=%p, pa=%p, size=%p\n",
			va, pa, size);
		return -1;
	}

	if (size == 0 || (size % PGSIZE) != 0)
	{
		printf("size not aligned: va=%p, pa=%p, size=%p\n",
			va, pa, size);
		return -1;
	}

	a = va;
	last = va + size - PGSIZE;
	for (;;) {
		pte = walk(pgdir, a, 1, user);
		if (pte == 0)
			return -1;
		if (*pte & PTE_P)
			return -1;
		*pte = PA2PTE(pa) | perm | PTE_P;
		if (a == last)
			break;
		a += PGSIZE;
		pa += PGSIZE;
	}
	return 0;
}

/*
 * 解除 va 起连续 npages 页的映射；do_free 非 0 时一并释放物理页。
 * 内核 / 用户页表共用，由 kvmunmap / uvmunmap 封装错误处理与 TLB 策略。
 */
static int unmappages(pagetable_t pgdir, uint va, uint npages, int do_free)
{
	pte_t *pte;
	uint a;

	if (pgdir == 0 || (va % PGSIZE) != 0 || npages == 0)
		return -1;

	for (a = va; a < va + npages * PGSIZE; a += PGSIZE) {
		pte = walk(pgdir, a, 0, 0);
		if (pte == 0 || !(*pte & PTE_P))
			return -1;
		if (do_free)
			free_page((void *)PTE_ADDR(*pte));
		*pte = 0;
	}
	return 0;
}

/* 仅当 pgdir 为当前 CR3 时刷新 TLB（用户页表卸载场景） */
static void tlb_flush_if_active(pagetable_t pgdir)
{
	if (r_cr3() == (uint)pgdir)
		w_cr3((uint)pgdir);
}

/* -------------------------------------------------------------------------- */
/* 内核地址空间                                                                */
/* -------------------------------------------------------------------------- */

void kvmmap(uint va, uint pa, uint size, int perm)
{
	if (mappages(kernel_pgdir, va, size, pa, perm, 0) < 0)
		panic("kvmmap");
}

void kvmunmap(uint va, uint size)
{
	// 内核恒等映射 [0, physmem_top) 时，很多物理页并不是“为这个映射专门分配的”
	// ——可能是 boot 区、MMIO、buddy 池里的页。kvmunmap 只拆映射，不能随便 free_page
	if ((va % PGSIZE) != 0 || size == 0 || (size % PGSIZE) != 0)
		panic("kvmunmap");
	if (unmappages(kernel_pgdir, va, size / PGSIZE, 0) < 0)
		panic("kvmunmap");

	/** 刷新 TLB */
	w_cr3((uint)kernel_pgdir);
}

/* 不保证物理连续，可能返回物理上不连续的内存 */
void *kvmalloc(uint va, int perm)
{
	(void)va;
	(void)perm;
	return 0;
}

/*
 * Build a two-level page table (page directory + page tables)
 * with a 1:1 map of [0, physmem_top).
 */
static void kvminit_map(void)
{
	// 映射范围为：1 * 1024(PDE) * 1024(PTE) * 4096(PAGE_SIZE) = 4GB
	static pde_t boot_pgdir[MAX_KERNEL_PT] __attribute__((aligned(PGSIZE)));

	kernel_pgdir = boot_pgdir;
	memset(kernel_pgdir, 0, PGSIZE);

	/** 分页机制开启，所有物理地址都恒等映射到内核虚拟地址空间 */
	kvmmap(0, 0, physmem_top, PTE_W);
}

/**
 * 内存页有没有映射和内存有没有分配是两回事：
 * - alloc_page 分配的是物理页；
 * - mappages 映射的是虚拟页；
 */
void kvm_init(void)
{
	kvminit_map();

	/* CPU 在写入 CR3 时，会自动刷新整个 TLB，使旧的虚拟到物理的映射失效 */
	w_cr3((uint)kernel_pgdir);
	w_cr0(r_cr0() | CR0_PG);
	printf("operating system paging enabled, kernel pgdir: %p\n", kernel_pgdir);

	// for (int i = 0; i < MAX_KERNEL_PT; i++) {
	// 	if (kernel_pgdir[i] != 0)
	// 		printf("  kpage dir[%d]: %p\n", i, kernel_pgdir[i]);
	// }
}

/* -------------------------------------------------------------------------- */
/* 用户地址空间                                                                */
/* -------------------------------------------------------------------------- */

pagetable_t uvmcreate(void)
{
	pagetable_t pgdir;
	int i;

	pgdir = (pagetable_t)alloc_page();
	if (pgdir == 0)
		return 0;
	memset(pgdir, 0, PGSIZE);

	/*
	 * 继承内核页目录项，使 trap 时 ISR 仍可执行
	 */
	for (i = 0; i < MAX_KERNEL_PT; i++) {
		uint va = (uint)i << 22;

		/* 跳过 [USERBASE, USEREND) 所在 PDE，避免与 uvmmap 的用户页共用
		 * 二级页表 */
		if (va >= USERBASE && va < USEREND)
			continue;

		/* 继承内核页目录项 */
		if (kernel_pgdir[i] & PTE_P)
			pgdir[i] = kernel_pgdir[i];
	}
	return pgdir;
}

/*
 * 将 kernel_pgdir 的监督者映射复制到 pgdir（跳过用户 VA 窗口）。
 * 供 fork 等场景复用。
 */
void uvmcopy_kernel(pagetable_t pgdir)
{
	int i;

	if (pgdir == 0)
		return;
	for (i = 0; i < MAX_KERNEL_PT; i++) {
		uint va = (uint)i << 22;

		if (va >= USERBASE && va < USEREND)
			continue;
		if (kernel_pgdir[i] & PTE_P)
			pgdir[i] = kernel_pgdir[i];
	}
}

/*
 * 复制父进程用户映射到子进程页表（fork）。
 * 仅复制 [USERBASE, sz) 内已映射的用户页，权限与父进程一致。
 */
int uvmcopy(pagetable_t old, pagetable_t new, uint sz)
{
	pte_t *pte;
	uint va, pa, i;
	char *mem;
	int perm;

	if (old == 0 || new == 0 || sz < USERBASE)
		return -1;
	if (sz > USEREND)
		sz = USEREND;

	for (va = USERBASE; va < sz; va += PGSIZE) {
		pte = walk(old, va, 0, 0);
		if (pte == 0 || !(*pte & PTE_P))
			continue;
		mem = alloc_page();
		if (mem == 0)
			return -1;
		pa = PTE_ADDR(*pte);
		for (i = 0; i < PGSIZE; i++)
			mem[i] = ((char *)pa)[i];
		perm = PTE_U | PTE_P;
		if (*pte & PTE_W)
			perm |= PTE_W;
		if (uvmmap(new, va, (uint)mem, PGSIZE, perm) < 0) {
			free_page(mem);
			return -1;
		}
	}
	return 0;
}

int uvmmap(pagetable_t pgdir, uint va, uint pa, uint size, int perm)
{
	if (pgdir == 0)
		return -1;
	return mappages(pgdir, va, size, pa, perm | PTE_U, 1);
}

int uvmunmap(pagetable_t pgdir, uint va, uint npages, int do_free)
{
	if (unmappages(pgdir, va, npages, do_free) < 0)
		return -1;
	/* 进程页表未必是当前 CR3，按需刷新 */
	tlb_flush_if_active(pgdir);
	return 0;
}

/*
 * 将 src（长度 sz）拷贝到新分配物理页，并以只读用户页映射到 va。
 * sz 须小于一页；失败时释放已分配物理页。
 */
int uvminit(pagetable_t pgdir, uint va, const void *src, uint sz)
{
	char *mem;
	uint i;

	if (pgdir == 0 || src == 0 || sz >= PGSIZE)
		return -1;

	mem = alloc_page();
	if (mem == 0)
		return -1;
	memset(mem, 0, PGSIZE);
	for (i = 0; i < sz; i++)
		mem[i] = ((const char *)src)[i];
	if (uvmmap(pgdir, va, (uint)mem, PGSIZE, PTE_P) < 0) {
		free_page(mem);
		return -1;
	}
	return 0;
}

/*
 * 返回 va 对应用户页的物理/线性地址（含页内偏移）；须 PTE_P 且 PTE_U。
 */
uint walkaddr(pagetable_t pgdir, uint va)
{
	pte_t *pte;

	if (pgdir == 0)
		return 0;
	pte = walk(pgdir, va, 0, 0);
	if (pte == 0 || !(*pte & PTE_P) || !(*pte & PTE_U))
		return 0;
	return PTE_ADDR(*pte) | (va & (PGSIZE - 1));
}

/*
 * 从用户页表 copyin 至多 n 字节到内核缓冲区；跨页自动拆分。
 */
int copyin(pagetable_t pgdir, void *dst, uint srcva, uint n)
{
	char *d = dst;
	uint va, pa, chunk;

	if (pgdir == 0 || dst == 0)
		return -1;
	while (n > 0) {
		uint i;

		va = PGROUNDDOWN(srcva);
		pa = walkaddr(pgdir, va);
		if (pa == 0)
			return -1;
		chunk = PGSIZE - (srcva - va);
		if (chunk > n)
			chunk = n;
		pa += srcva - va;
		for (i = 0; i < chunk; i++)
			d[i] = ((char *)pa)[i];
		d += chunk;
		srcva += chunk;
		n -= chunk;
	}
	return 0;
}

/*
 * 从内核缓冲区 copyout 至多 n 字节到用户页表；跨页自动拆分。
 */
int copyout(pagetable_t pgdir, uint dstva, const void *src, uint n)
{
	const char *s = src;
	uint va, pa, chunk;

	if (pgdir == 0 || src == 0)
		return -1;
	while (n > 0) {
		uint i;

		va = PGROUNDDOWN(dstva);
		pa = walkaddr(pgdir, va);
		if (pa == 0)
			return -1;
		chunk = PGSIZE - (dstva - va);
		if (chunk > n)
			chunk = n;
		pa += dstva - va;
		for (i = 0; i < chunk; i++)
			((char *)pa)[i] = s[i];
		s += chunk;
		dstva += chunk;
		n -= chunk;
	}
	return 0;
}

/*
 * 将 src（长度 sz）按页拷贝到 va 起连续用户页。
 * 权限：用户可读写（flat binary 含 .data/.bss，须可写；教学实现不区分 RX/RW）。
 * va 须页对齐；va+sz 不得超过 USEREND。
 */
int loaduvm(pagetable_t pgdir, uint va, const void *src, uint sz)
{
	char *mem;
	uint a, off, n;

	if (pgdir == 0 || src == 0 || sz == 0 || (va % PGSIZE) != 0)
		return -1;
	if (va < USERBASE || va + sz > USEREND)
		return -1;

	for (off = 0; off < sz; off += PGSIZE) {
		mem = alloc_page();
		if (mem == 0)
			return -1;
		memset(mem, 0, PGSIZE);
		n = sz - off;
		if (n > PGSIZE)
			n = PGSIZE;
		for (a = 0; a < n; a++)
			mem[a] = ((const char *)src + off)[a];
		a = va + off;
		if (uvmmap(pgdir, a, (uint)mem, PGSIZE, PTE_P | PTE_W) < 0) {
			free_page(mem);
			return -1;
		}
	}
	return 0;
}

/*
 * 释放 pgdir 中 [USERBASE, USEREND) 的用户映射及用户专用页表页；
 * 继承自 kernel_pgdir 的 PDE（无 PTE_U）保留不动，最后释放页目录。
 */
void uvmfree(pagetable_t pgdir)
{
	uint va;
	pte_t *pte;
	int i;

	if (pgdir == 0)
		return;

	for (va = USERBASE; va < USEREND; va += PGSIZE) {
		pte = walk(pgdir, va, 0, 0);
		if (pte == 0)
			continue;
		if (*pte & PTE_P) {
			free_page((void *)PTE_ADDR(*pte));
			*pte = 0;
		}
	}
	for (i = PDX(USERBASE); i <= PDX(USEREND - 1); i++) {
		if (pgdir[i] & PTE_P && (pgdir[i] & PTE_U)) {
			free_page((void *)PTE_ADDR(pgdir[i]));
			pgdir[i] = 0;
		}
	}
	free_page(pgdir);
}

/*
 * 从用户空间拷贝以 '\0' 结尾的字符串；max 含结尾 NUL 上限。
 */
int copyinstr(pagetable_t pgdir, char *dst, uint srcva, uint max)
{
	uint i;

	if (pgdir == 0 || dst == 0 || max == 0)
		return -1;
	for (i = 0; i + 1 < max; i++) {
		if (copyin(pgdir, dst + i, srcva + i, 1) < 0)
			return -1;
		if (dst[i] == '\0')
			return 0;
	}
	return -1;
}
