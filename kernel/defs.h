#include "types.h"
#include "riscv.h"

struct context;
struct spinlock;

/** bio.c */
void binit(void);
struct buf* bread(uint dev, uint blockno);
void bwrite(struct buf *b);
void brelse(struct buf *b);

/** console.c */
void consputc(int c);
int consolewrite(int user_src, uint64 src, int n);
int consoleread(int user_dst, uint64 dst, int n);
void consoleintr(int c);
void consoleinit(void);

/** file.c */
void fileinit(void);

/** fs.c */
void fsinit(int dev);
void iinit();

/** kalloc.c */
void kinit();
void *kalloc(void);
void kfree(void *pa);

/** main.c */
void main();

/** plic.c */
void plicinit(void);
void plicinithart(void);
int plic_claim(void);
void plic_complete(int irq);

/** printf.c */
void printf(char*, ...);
void panic(char*) __attribute__((noreturn));
void printfinit(void);

/** proc.c */
void procinit(void);
int cpuid();
struct cpu* mycpu(void);
struct proc* myproc(void);
int allocpid();
pagetable_t proc_pagetable(struct proc *p);
void proc_freepagetable(pagetable_t pagetable, uint64 sz);
void userinit(void);
void scheduler(void) __attribute__((noreturn));
void sched(void);
void yield(void);
void forkret(void);
void wakeup(void *chan);
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len);
int either_copyin(void *dst, int user_src, uint64 src, uint64 len);

/** swtch.S */
void swtch(struct context*, struct context*);

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
char* safestrcpy(char *s, const char *t, int n);
int strlen(const char *s);

/** trap.c */
void trapinit(void);
void trapinithart(void);
void usertrap(void);
void usertrapret(void);
void kerneltrap();

/** uart.c */
void uartinit(void);
void uartputc(int);
void uartputc_sync(int);
void uartstart();
int uartgetc(void);
void uartintr(void);

/** virtio_disk.c */
void virtio_disk_init(void);

/** vm.c */
void kvminit();
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc);
uint64 walkaddr(pagetable_t pagetable, uint64 va);
void kvmmap(uint64 va, uint64 pa, uint64 sz, int perm);
void kvminithart();
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm);
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free);
pagetable_t uvmcreate();
void uvminit(pagetable_t pagetable, uchar *src, uint sz);
void freewalk(pagetable_t pagetable);
void uvmfree(pagetable_t pagetable, uint64 sz);
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz);
void uvmclear(pagetable_t pagetable, uint64 va);
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len);
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len);
void dump_pagetable(void);