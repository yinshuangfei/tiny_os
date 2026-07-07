/*
 * 第一个内核线程 init（pid 1）：在 scheduler 启动后运行测试/后续用户态入口。
 * main/swapper（pid 0）只做硬件初始化，不调用 sleep/sched。
 */
#include "types.h"
#include "defs.h"
#include "proc.h"

void kernel_test(void);

static void init_main(void *arg)
{
	(void)arg;

	printf("init: pid=%d starting\n", myproc()->pid);
	kernel_test();
	printf("init: idle (pid=%d)\n", myproc()->pid);
	for (;;)
		sleep_ticks(100);
}

void init_start(void)
{
	initproc = kthread_create(init_main, 0, "init");
	if (!initproc)
		panic("init_start: kthread_create failed");
	initproc->parent = &proc_table[0];
	printf("init: created pid=%d, parent=swapper\n", initproc->pid);
}
