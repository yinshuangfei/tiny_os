#ifndef __PROC_LOCK_H__
#define __PROC_LOCK_H__

#include "spinlock.h"

/*
 * 保护进程表、就绪队列、pid 分配等 PCB 相关状态。
 * task_queue_*_locked() 须在持有此锁的临界区内调用。
 */
extern struct spinlock proc_lock;

#endif
