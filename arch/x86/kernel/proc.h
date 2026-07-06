#ifndef __PROC_H__
#define __PROC_H__

#include "types.h"
#include "param.h"
#include "list.h"
#include "vm.h"
#include "interrupt.h"

struct cpu {
	int id;
	struct proc *proc;
	int noff;		/* nested interrupt disable count */
	int intena;		/* interrupt enabled flag */
};

extern struct cpu cpus[NCPU];

struct cpu *mycpu(void);

/* swtch.S 保存的内核 callee-saved 寄存器（x86 32 位） */
struct context {
	uint eip;
	uint esp;
	uint ebx;
	uint esi;
	uint edi;
	uint ebp;
};

enum procstate {
	UNUSED,
	USED,
	SLEEPING,
	RUNNABLE,
	RUNNING,
	ZOMBIE,
};

struct proc {
	struct list_head list;	/* 全局 proc 链表 / 就绪队列节点 */
	enum procstate state;
	int pid;
	struct proc *parent;
	void *chan;		/* sleep/wakeup 等待通道；0 表示仅按时钟唤醒 */
	unsigned int wakeup_tick;	/* sleep_ticks 到期 tick；0 表示无超时 */
	int killed;
	int xstate;		/* exit 状态，wait 回收（预留） */

	pagetable_t pagetable;
	struct trapframe *kframe;	/* 内核态 trap 保存区（预留） */
	struct context context;
	void *kstack;		/* 内核栈页（预留） */
	uint sz;			/* 用户地址空间大小（预留） */
	char name[NNAME];
};

extern struct proc *proc_table;
extern struct list_head runqueue;

struct proc *myproc(void);

void procinit(void);
struct proc *proc_alloc(void);
void proc_free(struct proc *p);
struct proc *proc_find(int pid);

void sleep(void *chan);
void wakeup(void *chan);
void sleep_ticks(unsigned int nticks);
void proc_timer_tick(void);

#endif
