/*
 * 进程控制块（PCB）框架：proc 表由 kmalloc 动态分配，链表供调度器后续使用。
 */
#include "types.h"
#include "defs.h"
#include "param.h"
#include "list.h"
#include "proc.h"
#include "spinlock.h"

struct proc *proc_table;
struct list_head runqueue;

static struct spinlock proc_lock;
static int nextpid = 1;

void procinit(void)
{
	int i;

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
	printf("proc: table ready, nproc=%d (%d bytes)\n",
	       NPROC, NPROC * (uint)sizeof(struct proc));
}

struct proc *proc_alloc(void)
{
	int i;
	struct proc *p;

	acquire(&proc_lock);
	for (i = 0; i < NPROC; i++) {
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
