/*
 * 进程控制块（PCB）框架：proc 表由 kmalloc 动态分配，链表供调度器后续使用。
 */
#include "types.h"
#include "defs.h"
#include "param.h"
#include "list.h"
#include "proc.h"
#include "spinlock.h"
#include "timer.h"
#include "x86.h"

struct proc *proc_table;
struct list_head runqueue;

static struct spinlock proc_lock;
static int nextpid = 1;

struct proc *myproc(void)
{
	return mycpu()->proc;
}

static void sched_sleep(void)
{
	struct proc *p = myproc();

	for (;;) {
		acquire(&proc_lock);
		if (p->state != SLEEPING) {
			release(&proc_lock);
			return;
		}
		release(&proc_lock);
		sti();

		// isr_timer 的 iret 回到 halt 后面继续执行
		halt();
	}
}

void procinit(void)
{
	int i;
	struct proc *init;

	initlock(&proc_lock, "proc");
	INIT_LIST_HEAD(&runqueue);
	proc_table = kcalloc(NPROC, sizeof(struct proc));
	if (!proc_table)
		panic("procinit: out of memory");

	for (i = 0; i < NPROC; i++) {
		struct proc *p = &proc_table[i];

		INIT_LIST_HEAD(&p->list);
		p->state = UNUSED;
	}

	/* slot 0 作为内核/bootstrap 上下文，供 sleep_ticks 等使用 */
	init = &proc_table[0];
	init->state = RUNNABLE;
	init->pid = 0;
	mycpu()->proc = init;

	printf("proc: table ready, nproc=%d (%d bytes)\n",
	       NPROC, NPROC * (uint)sizeof(struct proc));
}

void proc_timer_tick(void)
{
	unsigned int now = timer_ticks();
	int i;
	struct proc *p;

	acquire(&proc_lock);
	for (i = 0; i < NPROC; i++) {
		p = &proc_table[i];
		if (p->state != SLEEPING)
			continue;
		if (p->wakeup_tick != 0 && now >= p->wakeup_tick) {
			p->state = RUNNABLE;
			p->wakeup_tick = 0;
			p->chan = 0;
		}
	}
	release(&proc_lock);
}

void sleep(void *chan)
{
	struct proc *p = myproc();

	if (!chan)
		panic("sleep: null chan");

	acquire(&proc_lock);
	p->chan = chan;
	p->wakeup_tick = 0;
	p->state = SLEEPING;
	release(&proc_lock);
	sched_sleep();
}

void wakeup(void *chan)
{
	int i;
	struct proc *p;

	if (!chan)
		return;

	acquire(&proc_lock);
	for (i = 0; i < NPROC; i++) {
		p = &proc_table[i];
		if (p->state == SLEEPING && p->chan == chan) {
			p->state = RUNNABLE;
			p->chan = 0;
			p->wakeup_tick = 0;
		}
	}
	release(&proc_lock);
}

void sleep_ticks(unsigned int nticks)
{
	struct proc *p = myproc();

	if (nticks == 0)
		return;

	acquire(&proc_lock);
	p->wakeup_tick = timer_ticks() + nticks;
	p->chan = 0;
	p->state = SLEEPING;
	release(&proc_lock);
	sched_sleep();
}

struct proc *proc_alloc(void)
{
	int i;
	struct proc *p;

	acquire(&proc_lock);
	for (i = 1; i < NPROC; i++) {
		p = &proc_table[i];
		if (p->state != UNUSED)
			continue;

		memset(p, 0, sizeof(*p));
		INIT_LIST_HEAD(&p->list);
		p->state = USED;
		p->pid = nextpid++;
		release(&proc_lock);
		return p;
	}
	release(&proc_lock);
	return 0;
}

void proc_free(struct proc *p)
{
	if (!p || p->state == UNUSED)
		return;

	acquire(&proc_lock);
	p->state = UNUSED;
	p->pid = 0;
	p->parent = 0;
	p->pagetable = 0;
	p->kstack = 0;
	p->name[0] = '\0';
	list_del(&p->list);
	INIT_LIST_HEAD(&p->list);
	release(&proc_lock);
}

struct proc *proc_find(int pid)
{
	int i;

	acquire(&proc_lock);
	for (i = 0; i < NPROC; i++) {
		if (proc_table[i].pid == pid &&
		    proc_table[i].state != UNUSED) {
			struct proc *p = &proc_table[i];

			release(&proc_lock);
			return p;
		}
	}
	release(&proc_lock);
	return 0;
}
