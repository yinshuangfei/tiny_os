#ifndef __PROC_H__
#define __PROC_H__

#include "types.h"
#include "param.h"
#include "list.h"
#include "mm/vm.h"
#include "trap.h"
#include "ipc/signal.h"

struct file;
struct inode;

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
	int id;
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
	char name[NNAME];
	struct file *ofile[NOFILE];	/* 打开文件表 */
	struct inode *cwd;		/* 当前工作目录 */

	/* 信号（ipc/signal.c） */
	uint sigpending;		/* 未决信号集 */
	uint sigmasked;			/* 阻塞信号集 */
	uint sighand[NSIG];		/* 信号处理函数数组 */
	struct trapframe *sigold;	/* 旧的 trapframe */
	int sighandling;		/* 是否正在处理信号 */
};

extern struct cpu cpus[NCPU];
struct cpu *mycpu(void);
extern const char *procstate_str[];
extern struct proc *proc_table;
extern struct proc *initproc;
extern struct proc *kthreadd_task;

void user_enter_ring3(struct proc *p) __attribute__((noreturn));

/* 当前运行在 ring3 的进程页表；trap 返回用户态前写回 CR3 */
extern pagetable_t current_user_pgdir;

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
