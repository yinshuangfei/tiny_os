#include "types.h"
#include "defs.h"
#include "memlayout.h"

/** page tables allocated before paging is enabled */
#define MAX_PT  64	// 最多能分配 64 张二级页表

// 二级页表池，每张二级页表有 1024 个 PTE
static pte_t pt_pool[MAX_PT][1024] __attribute__((aligned(PGSIZE)));
static int pt_used;

void *memset(void *dst, int c, uint n)
{
	uchar *p = dst;

	while (n-- > 0)
		*p++ = c;
	return dst;
}

void alloc_pt_reset(void)
{
	pt_used = 0;
}

pagetable_t alloc_pt(void)
{
	if (pt_used >= MAX_PT)
		return 0;
	memset(pt_pool[pt_used], 0, PGSIZE);
	return pt_pool[pt_used++];
}
