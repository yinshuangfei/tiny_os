/*
 * init 内核线程（pid 1）：类似 Linux kernel_init，kernel_execve 后变为用户态 init。
 */
#include "types.h"
#include "defs.h"
#include "proc.h"
#include "execve.h"

static void init_main(void *arg)
{
	(void)arg;

	printf("init: kernel pid=%d starting\n", myproc()->pid);

	/* 加载 initcode 并 iret 进入 ring3；成功则不返回 */
	kernel_execve(myproc(), "initcode");
	panic("init: kernel_execve initcode failed");
}

void init_start(void)
{
	initproc = kthread_create(init_main, 0, "init");
	if (!initproc)
		panic("init_start: kthread_create failed");
	initproc->parent = &proc_table[0];
	printf("init: created pid=%d, parent=swapper\n", initproc->pid);
}
