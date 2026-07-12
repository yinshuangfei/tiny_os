/*
 * 就绪队列：FIFO 双向循环链表。
 * 并发访问由 proc_lock 保护；本文件只实现队列逻辑，不加锁。
 */
#include "types.h"
#include "defs.h"
#include "list.h"
#include "proc.h"
#include "task_queue.h"

static struct list_head runqueue;

int on_list(struct proc *p)
{
	return p->list.next != &p->list;
}

void task_queue_init(void)
{
	INIT_LIST_HEAD(&runqueue);
}

void task_queue_unlink_locked(struct proc *p)
{
	if (on_list(p)) {
		list_del(&p->list);
		INIT_LIST_HEAD(&p->list);
	}
}

void task_queue_enqueue_locked(struct proc *p)
{
	if (p->state != RUNNABLE)
		panic("enqueue: not runnable");
	if (on_list(p))
		panic("enqueue: already queued");
	list_add_tail(&p->list, &runqueue);
}

struct proc *task_queue_dequeue_locked(void)
{
	struct list_head *node;
	struct proc *p;

	if (list_empty(&runqueue))
		return 0;
	node = runqueue.next;
	p = list_entry(node, struct proc, list);
	list_del(&p->list);
	INIT_LIST_HEAD(&p->list);
	return p;
}

int task_queue_empty_locked(void)
{
	return list_empty(&runqueue);
}

void task_queue_dump_locked(void)
{
	struct list_head *node;
	struct proc *p;

	printf("--------------------------------\n");
	printf("queue:\n");
	list_for_each(node, &runqueue) {
		p = list_entry(node, struct proc, list);
		printf("  pid:%d, %s\n", p->pid, procstate_str[p->state]);
	}
	printf("--------------------------------\n");
}
