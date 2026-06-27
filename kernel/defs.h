#include "types.h"
#include "riscv.h"

struct context;
struct spinlock;
struct sleeplock;
struct stat;

/** bio.c */
void binit(void);
struct buf* bread(uint dev, uint blockno);
void bwrite(struct buf *b);
void brelse(struct buf *b);
void bpin(struct buf *b);

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
struct inode* ialloc(uint dev, short type);
void iupdate(struct inode *ip);
struct inode* idup(struct inode *ip);
void ilock(struct inode *ip);
void iunlock(struct inode *ip);
void iput(struct inode *ip);
void iunlockput(struct inode *ip);
void itrunc(struct inode *ip);
void stati(struct inode *ip, struct stat *st);
int writei(struct inode *ip, int user_src, uint64 src, uint off, uint n);
int namecmp(const char *s, const char *t);
struct inode* dirlookup(struct inode *dp, char *name, uint *poff);
int dirlink(struct inode *dp, char *name, uint inum);
struct inode* namei(char *path);
struct inode* nameiparent(char *path, char *name);

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
void exit(int status);
void scheduler(void) __attribute__((noreturn));
void sched(void);
void yield(void);
void forkret(void);
void sleep(void *chan, struct spinlock *lk);
void wakeup(void *chan);
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len);
int either_copyin(void *dst, int user_src, uint64 src, uint64 len);

/** sleeplock.h */
void initsleeplock(struct sleeplock *lk, char *name);
void acquiresleep(struct sleeplock *lk);
void releasesleep(struct sleeplock *lk);
int holdingsleep(struct sleeplock *lk);

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
int memcmp(const void *v1, const void *v2, uint n);
void *memmove(void *dst, const void *src, uint n);
void* memcpy(void *dst, const void *src, uint n);
int strncmp(const char *p, const char *q, uint n);
char* strncpy(char *s, const char *t, int n);
char* safestrcpy(char *s, const char *t, int n);
int strlen(const char *s);

/** syscall.c */
int fetchaddr(uint64, uint64*);
int fetchstr(uint64, char*, int);
int argint(int, int*);
int argaddr(int, uint64 *);
int argstr(int, char*, int);
void syscall(void);

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
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max);
void dump_pagetable(void);

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))
