#ifndef __PROC_H__
#define __PROC_H__

#include "types.h"
#include "param.h"
#include "list.h"
#include "vm.h"
#include "interrupt.h"

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
	int noff;
	int intena;
	int need_resched;	/* 定时器 tick 请求抢占 */
};

extern struct cpu cpus[NCPU];

struct cpu *mycpu(void);

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

extern const char *procstate_str[];

struct proc {
	struct list_head list;		/* 就绪队列节点 */
	enum procstate state;
	int pid;
	struct proc *parent;
	void *chan;
	unsigned int wakeup_tick;
	int killed;
	int xstate;			/* 退出状态 */

	pagetable_t pagetable;		/* 用户页表 */
	struct trapframe *kframe;	/* 内核栈顶的 trapframe */
	struct context context;
	void *kstack;			/* 内核栈 */
	void (*entry)(void *);
	void *entry_arg;
	uint sz;			/* 用户虚拟空间大小 */
	char name[NNAME];
};

extern struct proc *proc_table;
extern struct proc *initproc;
extern struct list_head runqueue;

/* 当前运行在 ring3 的进程页表；trap 返回用户态前写回 CR3 */
extern pagetable_t current_user_pgdir;

struct proc *myproc(void);

void procinit(void);
void init_start(void);
struct proc *proc_alloc(void);
void proc_free(struct proc *p);
struct proc *proc_find(int pid);

void exit(int status) __attribute__((noreturn));

void sleep(void *chan);
void wakeup(void *chan);
void sleep_ticks(unsigned int nticks);
void sched_tick(void);
void preempt_check(void);

void sched(void);
void context_switch(struct context *old, struct context *new);
void yield(void);
void scheduler(void) __attribute__((noreturn));

struct proc *kthread_create(void (*fn)(void *), void *arg, const char *name);
void kthread_exit(void);

void start_first_user(void) __attribute__((noreturn));

#endif
