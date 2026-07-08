#ifndef __MMU_H__
#define __MMU_H__

#include "types.h"

/** page directory / page table index, 10 bits */
/** 页目录索引，32 位虚拟地址的高 10 位 */
/** 页表索引，32 位虚拟地址的中间 10 位 */
#define PDX(va)   (((uint)(va) >> 22) & 0x3ff)	/* 页目录索引 */
#define PTX(va)   (((uint)(va) >> 12) & 0x3ff)	/* 页表索引 */

/** extract physical address from a PTE/PDE */
#define PTE_ADDR(pte)   ((uint)(pte) & 0xfffff000)	// physical address of the page
#define PA2PTE(pa)      ((uint)(pa) & 0xfffff000)	// physical address to PTE

/** PTE/PDE flags */
#define PTE_P   0x001   /* present */
#define PTE_W   0x002   /* writable */
#define PTE_U   0x004   /* user accessible */
#define PTE_PWT 0x008	/* write-through */
#define PTE_PCD 0x010	/* cache disabled */
#define PTE_A   0x020	/* accessed */
#define PTE_D   0x040	/* dirty */
#define PTE_PS  0x080	/* page size */
#define PTE_G   0x100	/* global */

#endif
