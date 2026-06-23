#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "memlayout.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

/*
 * create a direct-map page table for the kernel.
 */
void kvminit()
{
	/** 4KB pagetable, 512 PTEs, 8Bytes(64bits)/per PTE */
	kernel_pagetable = (pagetable_t) kalloc();
	memset(kernel_pagetable, 0, PGSIZE);

	// uart registers
	kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

	// virtio mmio disk interface
	kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

	// CLINT
	kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

	// PLIC
	kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

	// map kernel text executable and read-only.
	kvmmap(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

	// map kernel data and the physical RAM we'll make use of.
	kvmmap((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

	// map the trampoline for trap entry/exit to
	// the highest virtual address in the kernel.
	kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void kvminithart()
{
	w_satp(MAKE_SATP(kernel_pagetable));
	sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
// 总的寻址空间为 512G = 512*512*512*4K
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc)
{
	if (va >= MAXVA)
		panic("walk");

	for (int level = 2; level > 0; level--) {
		pte_t *pte = &pagetable[PX(level, va)];
		if (*pte & PTE_V) {
			pagetable = (pagetable_t)PTE2PA(*pte);
		} else {
			if (!alloc || (pagetable = (pde_t*)kalloc()) == 0)
				return 0;
			memset(pagetable, 0, PGSIZE);
			*pte = PA2PTE(pagetable) | PTE_V;
		}
	}
	return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64 walkaddr(pagetable_t pagetable, uint64 va)
{
	pte_t *pte;
	uint64 pa;

	if(va >= MAXVA)
		return 0;

	pte = walk(pagetable, va, 0);
	if (pte == 0)
		return 0;
	if ((*pte & PTE_V) == 0)
		return 0;
	if ((*pte & PTE_U) == 0)
		return 0;
	pa = PTE2PA(*pte);
	return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
	if(mappages(kernel_pagetable, va, sz, pa, perm) != 0)
		panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
	uint64 a, last;
	pte_t *pte;

	printf("mmap: va:0x%lx, pa:0x%lx, size:%ld\n", va, pa, size);

	a = PGROUNDDOWN(va);
	last = PGROUNDDOWN(va + size - 1);

	for (;;) {
		if ((pte = walk(pagetable, a, 1)) == 0)
			return -1;
		if (*pte & PTE_V)
			panic("remap");
		*pte = PA2PTE(pa) | perm | PTE_V;
		if (a == last)
			break;
		a += PGSIZE;
		pa += PGSIZE;
	}
	return 0;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
	uint64 n, va0, pa0;

	while (len > 0) {
		va0 = PGROUNDDOWN(dstva);
		pa0 = walkaddr(pagetable, va0);
		if (pa0 == 0)
			return -1;

		n = PGSIZE - (dstva - va0);
		if (n > len)
			n = len;

		memmove((void *)(pa0 + (dstva - va0)), src, n);

		len -= n;
		src += n;
		dstva = va0 + PGSIZE;
	}
	return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
	uint64 n, va0, pa0;

	while (len > 0) {
		va0 = PGROUNDDOWN(srcva);
		pa0 = walkaddr(pagetable, va0);
		if (pa0 == 0)
			return -1;

		n = PGSIZE - (srcva - va0);
		if (n > len)
			n = len;

		memmove(dst, (void *)(pa0 + (srcva - va0)), n);

		len -= n;
		dst += n;
		srcva = va0 + PGSIZE;
	}
	return 0;
}