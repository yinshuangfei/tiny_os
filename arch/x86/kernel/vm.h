#ifndef __VM_H__
#define __VM_H__

#include "types.h"

/*
 * 内核地址空间（kernel_pgdir，kvm_init 后恒等映射 [0, physmem_top)）
 * 用于 MMIO、内核数据结构、临时映射等。
 */
void kvm_init(void);
void kvmmap(uint va, uint pa, uint size, int perm);
void kvmunmap(uint va, uint size);
void *kvmalloc(uint va, int perm);

extern pagetable_t kernel_pgdir;

/*
 * 用户地址空间（每进程独立页表，PTE_U 映射）
 * 后续进程 / exec / fork 使用。
 */
pagetable_t uvmcreate(void);
int uvmmap(pagetable_t pgdir, uint va, uint pa, uint size, int perm);
int uvmunmap(pagetable_t pgdir, uint va, uint npages, int do_free);
int uvminit(pagetable_t pgdir, uint va, const void *src, uint sz);
void uvmcopy_kernel(pagetable_t pgdir);

#endif
