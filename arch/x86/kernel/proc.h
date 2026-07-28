#ifndef __PROC_H__
#define __PROC_H__

#include "types.h"
#include "param.h"
#include "list.h"
#include "mm/vm.h"
#include "trap.h"
#include "ipc/signal.h"
#include "fpu.h"

struct file;
struct inode;

/* 用户 mmap 区域（对齐 Linux vm_area_struct 教学子集） */
struct vma {
	int used;			/* 是否被使用 */
	uint start;			/* [start, end) 页对齐 */
	uint end;			/* [start, end) 页对齐 */
	int prot;			/* 保护标志 PROT_READ, PROT_WRITE, PROT_EXEC */
	/*
	 * 低 16 位：MAP_*；高 16 位：SysV shmid+1（0 表示非 shm）。
	 * 编码进 flags，避免扩大本结构体（防止未重编 .o 布局错位）。
	 */
	int flags;
};

/* 取 VMA 上的 SysV shmid；非 shm 返回 -1 */
static inline int vma_shmid(const struct vma *v)
{
	unsigned id;

	if (!v || !v->used)
		return -1;
	id = ((unsigned)v->flags >> 16) & 0xffffu;
	return id ? (int)(id - 1) : -1;
}

/* 在 flags 低 16 位 MAP_* 上编码 shmid（shmid<0 表示清除） */
static inline int vma_flags_with_shmid(int flags, int shmid)
{
	flags &= 0xffff;
	if (shmid >= 0)
		flags |= ((shmid + 1) & 0xffff) << 16;
	return flags;
}

/* 用 X-Macro 维护一份 PROCSTATE_LIST */
#define PROCSTATE_LIST \
	X(UNUSED) \
	X(USED) \
	X(SLEEPING) \
	X(RUNNABLE) \
	X(RUNNING) \
	X(ZOMBIE)

enum procstate {
#define X(name) name,
	PROCSTATE_LIST
#undef X
	NPROCSTATE
};

/* swtch.S 保存的内核 callee-saved 寄存器（x86 32 位） */
struct context {
	uint eip;	/* offset  0, 指令指针 */
	uint esp;	/* offset  4, 内核栈指针 ss:sp, sp 是栈顶指针 */
	uint ebx;	/* offset  8, 基址寄存器 */
	uint esi;	/* offset 12, 源索引寄存器 */
	uint edi;	/* offset 16, 目标索引寄存器 */
	uint ebp;	/* offset 20, 基址指针寄存器 */
};

struct cpu {
	int id;			/* 逻辑 CPU 号 */
	int apicid;		/* Local APIC ID */
	struct proc *proc;	/* 当前运行进程, 内核态及用户态 */
	struct proc *last_sched;/* 最近一次 sched() 的进程，供回收 ZOMBIE */
	struct context context;	/* scheduler 上下文，swtch 回到此处 */
	int noff;		/* 中断嵌套层数 */
	int intena;
	int need_resched;	/* 定时器 tick 请求抢占 */
};

/* 进程结构体
 * 在 Linux 内核里，struct proc 对应的主要结构体是 struct task_struct.
*/
struct proc {
	struct list_head list;		/* 就绪队列节点 */
	enum procstate state;
	int pid;
	struct proc *parent;
	void *chan;			/* 睡眠通道 */
	unsigned int wakeup_tick;	/* 唤醒时间戳 */
	int killed;
	int xstate;			/* 退出状态 */

	pagetable_t pagetable;		/* 用户页表 */
	struct trapframe *kframe;	/* 指向内核栈的栈顶的 trapframe */
	struct context context;		/* 进程上下文 */
	void *kstack;			/* 内核栈（用户线程及内核线程） */
	void (*entry)(void *);
	void *entry_arg;
	uint sz;			/* 用户 VA 扫描上界（fork/COW，现为 USERSTACK） */
	uint brk;			/* 程序间断点（heap end，类 Linux mm->brk） */
	uint brk_start;			/* exec 后初始 brk（不可再缩小到其下） */
	struct vma vmas[NVMA];		/* 匿名 mmap 区域表 */

	/* FPU/SSE（FXSAVE 区须 16 字节对齐） */
	int fpu_used;			/* 1：fpu_state 有效（曾用过或继承） */
	/* fxsave 存储的内容，区须 16 字节对齐 */
	__attribute__((aligned(16))) unsigned char fpu_state[FX_SIZE];

	char name[NNAME];
	struct file *ofile[NOFILE];	/* 打开文件表 */
	unsigned char fdflags[NOFILE];	/* 每 fd 标志（FD_CLOEXEC） */
	struct inode *cwd;		/* 当前工作目录 */

	/* 信号（ipc/signal.c） */
	uint sigpending;		/* 未决信号集 */
	uint sigmasked;			/* 阻塞信号集 */
	uint sighand[NSIG];		/* 信号处理函数数组 */
	struct trapframe *sigold;	/* 旧的 trapframe */
	int sighandling;		/* 是否正在处理信号 */
};

extern struct cpu cpus[NR_CPUS];
struct cpu *mycpu(void);
extern const char *procstate_str[];
extern struct proc *proc_table;
extern struct proc *initproc;
extern struct proc *kthreadd_task;

void user_enter_ring3(struct proc *p) __attribute__((noreturn));

/* 当前运行在 ring3 的进程页表（每 CPU）；trap 返回用户态前写回 CR3 */
extern pagetable_t cpu_user_pgdir[NR_CPUS];
pagetable_t get_user_pgdir(void);
void set_user_pgdir(pagetable_t pgdir);

struct proc *myproc(void);

void procinit(void);
void rest_init(void);
struct proc *proc_alloc(void);
void proc_free(struct proc *p);
struct proc *proc_find(int pid);

void exit(int status) __attribute__((noreturn));
void exit_signal(int sig) __attribute__((noreturn));

void sleep(void *chan);
void wakeup(void *chan);
void sleep_ticks(unsigned int nticks);
void sleep_deadline(unsigned int deadline);
void sched_tick(void);
void preempt_check(void);

void sched(void);
void context_switch(struct context *old, struct context *new);
void yield(void);
void scheduler(void) __attribute__((noreturn));

void kthreadd(void *arg);
struct proc *kthread_create(void (*fn)(void *), void *arg, const char *name);
void kthread_exit(void);

int fork_copy(struct trapframe *tf);
int wait_child(int pid, int *status, struct proc *parent);

/** 打印就绪队列 */
void dump_runqueue(void);

#endif
