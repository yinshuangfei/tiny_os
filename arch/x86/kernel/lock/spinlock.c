#include "../types.h"
#include "../defs.h"
#include "spinlock.h"
#include "../proc.h"
#include "../x86.h"

struct cpu *mycpu(void)
{
	return &cpus[0];
}

void initlock(struct spinlock *lk, char *name)
{
	lk->name = name;
	lk->locked = 0;
	lk->cpu = 0;
}

void acquire(struct spinlock *lk)
{
	push_off();

	if (holding(lk))
		panic("acquire");

	while (__sync_lock_test_and_set(&lk->locked, 1) != 0)
		;

	__sync_synchronize();
	lk->cpu = mycpu();
}

void release(struct spinlock *lk)
{
	if (!holding(lk))
		panic("release");

	lk->cpu = 0;
	__sync_synchronize();
	__sync_lock_release(&lk->locked);
	pop_off();
}

int holding(struct spinlock *lk)
{
	return lk->locked && lk->cpu == mycpu();
}

/**
 * push_off - （CPU 级别）关闭中断并增加中断计数器
 * 保存当前中断状态，并关闭中断。
 * 如果中断计数器为 0，则保存当前中断使能状态。
 * 增加中断计数器。
 */
void push_off(void)
{
	int old = intr_get();

	intr_off();
	if (mycpu()->noff == 0)
		mycpu()->intena = old;
	mycpu()->noff++;
}

/**
 * pop_off - （CPU 级别）恢复中断状态并减少中断计数器
 * 检查是否在中断模式下执行，如果在中断模式下执行，则panic。
 * 检查中断计数器是否大于 0，如果小于 0，则panic。
 * 减少中断计数器。
 * 如果中断计数器为 0，则恢复中断使能状态。
 */
void pop_off(void)
{
	struct cpu *c = mycpu();

	if (intr_get())
		panic("pop_off - interruptible");
	if (c->noff < 1)
		panic("pop_off");
	c->noff--;
	if (c->noff == 0 && c->intena) {
		preempt_check();
		intr_on();
	}
}
