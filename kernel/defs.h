#include "types.h"
#include "riscv.h"

struct spinlock;

/** console.c */
void consputc(int c);
int consolewrite(int user_src, uint64 src, int n);
int consoleread(int user_dst, uint64 dst, int n);
void consoleinit(void);

/** kalloc.c */
void kinit();
void *kalloc(void);
void kfree(void *pa);

/** main.c */
void main();

/** printf.c */
void printf(char*, ...);
void panic(char *s);
void printfinit(void);

/** proc.c */
void procinit(void);
int cpuid();
struct cpu* mycpu(void);
struct proc* myproc(void);
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len);
int either_copyin(void *dst, int user_src, uint64 src, uint64 len);

/** spinlock.c */
void initlock(struct spinlock*, char*);
void acquire(struct spinlock *lk);
void release(struct spinlock *lk);
int holding(struct spinlock *lk);
void push_off(void);
void pop_off(void);

/** string.c */
void *memset(void *dst, int c, uint n);
void *memmove(void *dst, const void *src, uint n);

/** uart.c */
void uartinit(void);
void uartputc(int);
void uartputc_sync(int);

/** vm.c */
void kvminit();
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc);
uint64 walkaddr(pagetable_t pagetable, uint64 va);
void kvmmap(uint64 va, uint64 pa, uint64 sz, int perm);
void kvminithart();
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm);
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len);
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len);