/*
 * init 内核线程（pid 1）：创建用户子进程、yield 让出 CPU、wait/reap 回收。
 */
#include "types.h"
#include "defs.h"
#include "proc.h"

void kernel_test(void);

// static void ttt(void *arg)
// {
// 	(void)arg;

// 	while (1) {
// 		kernel_test();
// 		sleep_ticks(150);
// 	}
// }

static void init_main(void *arg)
{
	(void)arg;

	printf("init: pid=%d starting\n", myproc()->pid);

	// struct proc *t = kthread_create(ttt, 0, "ttt");
	// if (!t)
	// 	panic("ttt: kthread_create failed");

	for (;;) {
		printf("init: launching user program\n");
		start_first_user();
		yield();		/* 入就绪队列，让 scheduler 调度 loader */
		init_wait_children();
		printf("init: all children exited\n");
	}
}

void init_start(void)
{
	initproc = kthread_create(init_main, 0, "init");
	if (!initproc)
		panic("init_start: kthread_create failed");
	initproc->parent = &proc_table[0];
	printf("init: created pid=%d, parent=swapper\n", initproc->pid);
}
