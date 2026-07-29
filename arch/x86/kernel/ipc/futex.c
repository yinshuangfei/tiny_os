/*
 * futex：用户态同步原语（POSIX 信号量等库的阻塞后端）。
 *
 * futex(uaddr, op, val, timeout, uaddr2, val3)
 *   FUTEX_WAIT：若 *uaddr == val 则睡眠；已被改过则立即返回 0
 *   FUTEX_WAKE：唤醒最多 val 个在同一物理字上等待的进程
 *
 * timeout / uaddr2 / val3：教学版忽略。
 */
#include "../types.h"
#include "../defs.h"
#include "../param.h"
#include "../proc.h"
#include "../syscall.h"
#include "../lock/spinlock.h"
#include "../lock/proc_lock.h"
#include "../task_queue.h"
#include "../mm/vm.h"
#include "futex.h"

static struct spinlock futex_lock;
static int futex_inited;

static void futex_init(void)
{
	if (futex_inited)
		return;
	initlock(&futex_lock, "futex");
	futex_inited = 1;
}

/* 用户 VA → 物理字地址（作等待通道）；失败返回 0 */
static void *futex_key(struct proc *p, uint uaddr)
{
	uint pa;

	if (!p || !p->pagetable)
		return 0;
	if (uaddr & 3)
		return 0;
	/* copyin 会 ensure 页存在 */
	{
		int dummy;

		if (copyin(p->pagetable, &dummy, uaddr, sizeof(dummy)) < 0)
			return 0;
	}
	pa = walkaddr(p->pagetable, uaddr);
	if (pa == 0)
		return 0;
	return (void *)pa;
}

static void futex_sleep(void *chan)
{
	struct proc *p = myproc();

	if (!holding(&futex_lock))
		panic("futex_sleep");
	if (!chan)
		panic("futex_sleep: null");

	acquire(&proc_lock);
	release(&futex_lock);
	p->chan = chan;
	p->wakeup_tick = 0;
	p->state = SLEEPING;
	release(&proc_lock);
	sched();
	acquire(&futex_lock);
}

static int do_futex_wait(uint uaddr, int val)
{
	struct proc *p = myproc();
	void *key;
	int cur;

	futex_init();
	acquire(&futex_lock);
	key = futex_key(p, uaddr);
	if (!key) {
		release(&futex_lock);
		return -1;
	}
	if (copyin(p->pagetable, &cur, uaddr, sizeof(cur)) < 0) {
		release(&futex_lock);
		return -1;
	}
	if (cur != val) {
		release(&futex_lock);
		return 0;
	}
	futex_sleep(key);
	release(&futex_lock);
	return 0;
}

static int do_futex_wake(uint uaddr, int nwake)
{
	struct proc *p = myproc();
	void *key;
	int i, woken = 0;

	if (nwake <= 0)
		return 0;

	futex_init();
	acquire(&futex_lock);
	key = futex_key(p, uaddr);
	if (!key) {
		release(&futex_lock);
		return -1;
	}

	acquire(&proc_lock);
	for (i = 0; i < NPROC && woken < nwake; i++) {
		struct proc *t = &proc_table[i];

		if (t->state == SLEEPING && t->chan == key) {
			t->state = RUNNABLE;
			t->chan = 0;
			t->wakeup_tick = 0;
			task_queue_enqueue_locked(t);
			woken++;
		}
	}
	release(&proc_lock);
	release(&futex_lock);
	return woken;
}

int sys_futex(struct trapframe *tf)
{
	uint uaddr;
	int op, val;

	if (argaddr(tf, 0, &uaddr) < 0)
		return -1;
	if (argint(tf, 1, &op) < 0)
		return -1;
	if (argint(tf, 2, &val) < 0)
		return -1;

	op &= FUTEX_CMD_MASK;
	switch (op) {
	case FUTEX_WAIT:
		return do_futex_wait(uaddr, val);
	case FUTEX_WAKE:
		return do_futex_wake(uaddr, val);
	default:
		return -1;
	}
}
