/**
 * x86 32 位两级页表里，PDE 和 PTE 格式相同，都是 32 位（4 字节）：
 * 31          12 11    9 8 7 6 5 4 3 2 1 0
 * ┌──────────────┬──────┬─┬─┬─┬─┬─┬─┬─┬─┬─┐
 * │  物理页号     │  AVL │G│P│0│A│D│W│U│P│
 * └──────────────┴──────┴─┴─┴─┴─┴─┴─┴─┴─┘
*/

#include "types.h"
#include "defs.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"

pagetable_t kernel_pgdir;

/* -------------------------------------------------------------------------- */
/* 页表遍历与映射（内核 / 用户页表共用）                                        */
/* -------------------------------------------------------------------------- */

/*
 * Return the address of the PTE for virtual address va.
 * Create intermediate page-table pages when alloc != 0.
 */
static pte_t *walk(pagetable_t pgdir, uint va, int alloc)
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
		*pde = PA2PTE((uint)pt) | PTE_P | PTE_W;
	}
	return &pt[PTX(va)];
}

static int mappages(pagetable_t pgdir, uint va, uint size, uint pa, int perm)
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
		pte = walk(pgdir, a, 1);
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

static int unmappages(pagetable_t pgdir, uint va, uint size)
{
	pte_t *pte;
	uint a, last;

	if ((va % PGSIZE) != 0) {
		printf("page not aligned: va=%p, size=%p\n", (void *)va, (void *)size);
		return -1;
	}
	if (size == 0 || (size % PGSIZE) != 0) {
		printf("size not aligned: va=%p, size=%p\n", (void *)va, (void *)size);
		return -1;
	}

	a = va;
	last = va + size - PGSIZE;
	for (;;) {
		pte = walk(pgdir, a, 0);
		if (pte == 0 || !(*pte & PTE_P))
			return -1;
		*pte = 0;
		if (a == last)
			break;
		a += PGSIZE;
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* 内核地址空间                                                                 */
/* -------------------------------------------------------------------------- */

void kvmmap(uint va, uint pa, uint size, int perm)
{
	if (mappages(kernel_pgdir, va, size, pa, perm) < 0)
		panic("kvmmap");
}

void kvmunmap(uint va, uint size)
{
	if (unmappages(kernel_pgdir, va, size) < 0)
		panic("kvmunmap");
	/** 刷新 TLB */
	w_cr3((uint)kernel_pgdir);
}

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

	/** 分页机制开启，所有物理地址都映射到内核虚拟地址空间 */
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

	for (int i = 0; i < MAX_KERNEL_PT; i++) {
		if (kernel_pgdir[i] != 0)
			printf("  kpage dir[%d]: %p\n", i, kernel_pgdir[i]);
	}
}

/* -------------------------------------------------------------------------- */
/* 用户地址空间                                                                 */
/* -------------------------------------------------------------------------- */

pagetable_t uvmcreate(void)
{
	return 0;
}
