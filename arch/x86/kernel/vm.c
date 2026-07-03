#include "types.h"
#include "defs.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"

pagetable_t kernel_pgdir;

/** page tables allocated before paging is enabled */
#define MAX_PT  64
static pte_t pt_pool[MAX_PT][1024] __attribute__((aligned(PGSIZE)));
static int pt_used;

static void *memset(void *dst, int c, uint n)
{
	uchar *p = dst;

	while (n-- > 0)
		*p++ = c;
	return dst;
}

static pte_t *alloc_pt(void)
{
	if (pt_used >= MAX_PT)
		return 0;
	memset(pt_pool[pt_used], 0, PGSIZE);
	return pt_pool[pt_used++];
}

/*
 * Return the address of the PTE for virtual address va.
 * Create intermediate page-table pages when alloc != 0.
 */
static pte_t *walk(pagetable_t pgdir, uint va, int alloc)
{
	pde_t *pde = &pgdir[PDX(va)];

	if (*pde & PTE_P)
		pgdir = (pagetable_t)PTE_ADDR(*pde);
	else {
		if (!alloc)
			return 0;
		pgdir = (pagetable_t)alloc_pt();
		if (pgdir == 0)
			return 0;
		*pde = PA2PTE((uint)pgdir) | PTE_P | PTE_W;
	}
	return &pgdir[PTX(va)];
}

static int mappages(pagetable_t pgdir, uint va, uint size, uint pa, int perm)
{
	pte_t *pte;
	uint a, last;

	if ((va % PGSIZE) != 0 || (pa % PGSIZE) != 0)
		return -1;
	if (size == 0 || (size % PGSIZE) != 0)
		return -1;

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

static void kvmmap(uint va, uint pa, uint size, int perm)
{
	if (mappages(kernel_pgdir, va, size, pa, perm) < 0)
		panic("kvmmap");
}

/*
 * Build a two-level page table (page directory + page tables)
 * with a 1:1 map of [0, PHYSTOP).
 */
void kvminit(void)
{
	static pde_t boot_pgdir[512] __attribute__((aligned(PGSIZE)));

	kernel_pgdir = boot_pgdir;
	memset(kernel_pgdir, 0, PGSIZE);
	pt_used = 0;

	kvmmap(0, 0, PHYSTOP, PTE_W);
}

void kvminithart(void)
{
	/* CPU 在写入 CR3 时，会自动刷新整个 TLB，使旧的虚拟到物理的映射失效 */
	w_cr3((uint)kernel_pgdir);
	w_cr0(r_cr0() | CR0_PG);

	/* 故意访问未映射的地址 */
	printf("Triggering page fault...\n");
	// int *bad_ptr = (int *)0xDEADBEEF;
	// *bad_ptr = 42; // 这里必须触发 #PF (Page Fault)

	printf("Triggering divide error...\n");
	// int a = 1 / 0;
	// printf("a = %d\n", a);
}
