/*
 * init 内核线程（pid 1）：拉起用户 init，yield 让出 CPU，并作为后备收尸。
 */
#include "types.h"
#include "defs.h"
#include "proc.h"

void kernel_test(void);

static void init_main(void *arg)
{
	(void)arg;

	printf("init: kernel pid=%d starting\n", myproc()->pid);

	uthread_create_init();

	for (;;) {
		yield();
		init_wait_children();
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
