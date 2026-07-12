#ifndef __TASK_QUEUE_H__
#define __TASK_QUEUE_H__

struct proc;

int on_list(struct proc *p);
void task_queue_init(void);

/*
 * 以下接口假定调用方已持有 proc_lock（见 proc_lock.h）。
 * 通常与修改 p->state、扫描 proc_table 放在同一临界区。
 */
void task_queue_enqueue_locked(struct proc *p);
struct proc *task_queue_dequeue_locked(void);
int task_queue_empty_locked(void);
void task_queue_unlink_locked(struct proc *p);
void task_queue_dump_locked(void);

#endif
