#ifndef __VM_H__
#define __VM_H__

#include "../types.h"

struct proc;
struct vma;

extern pagetable_t kernel_pgdir;

/*
 * 内核地址空间（kernel_pgdir，kvm_init 后恒等映射 [0, physmem_top)）
 * 用于 MMIO、内核数据结构、临时映射等。
 */
void kvm_init(void);
void kvmmap(uint va, uint pa, uint size, int perm);
void kvmunmap(uint va, uint size);
void *kvmalloc(uint va, int perm);

/*
 * 用户地址空间（每进程独立页表，PTE_U 映射）
 * 后续进程 / execve / fork 使用。
 */
pagetable_t uvmcreate(void);
int uvmmap(pagetable_t pgdir, uint va, uint pa, uint size, int perm);
int uvmunmap(pagetable_t pgdir, uint va, uint npages, int do_free);
int uvminit(pagetable_t pgdir, uint va, const void *src, uint sz);
int loaduvm(pagetable_t pgdir, uint va, const void *src, uint sz);
int loaduvm_seg(pagetable_t pgdir, uint va, const void *src, uint filesz,
		uint memsz, int perm);
void uvmfree(pagetable_t pgdir);
void uvmcopy_kernel(pagetable_t pgdir);
int uvmcopy(pagetable_t old, pagetable_t new, uint sz);
int uvm_cow_fault(pagetable_t pgdir, uint va);
int uvm_demand_fault(struct proc *p, uint va, int write);
uint uvmalloc(pagetable_t pgdir, uint oldsz, uint newsz);
uint uvmdealloc(pagetable_t pgdir, uint oldsz, uint newsz);

uint walkaddr(pagetable_t pgdir, uint va);
int copyin(pagetable_t pgdir, void *dst, uint srcva, uint n);
int copyout(pagetable_t pgdir, uint dstva, const void *src, uint n);
int copyinstr(pagetable_t pgdir, char *dst, uint srcva, uint max);

/* 匿名 mmap（mm/mmap.c） */
void vma_clear(struct proc *p);
void vma_copy(struct proc *dst, struct proc *src);
struct vma *vma_lookup(struct proc *p, uint va);
int vma_overlaps_brk(struct proc *p, uint old_brk, uint new_brk);
uint mmap_find_addr(struct proc *p, uint len);
int mmap_region_ok(struct proc *p, uint start, uint end);
struct vma *vma_create(struct proc *p, uint start, uint end,
		       int prot, int flags, int shmid);
uint do_mmap(struct proc *p, uint addr, uint len, int prot, int flags,
	     int fd, uint pgoff);
int do_munmap(struct proc *p, uint addr, uint len);

#endif
