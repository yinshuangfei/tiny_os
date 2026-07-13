/*
 * init / kthreadd 启动（类似 Linux rest_init）：
 *   rest_init()   — 依次创建 PID1(init)、PID2(kthreadd)
 *   kernel_init() — PID1 线程体，kernel_execve 后变为用户态 init
 *   kthreadd()    — PID2，消费内核线程创建请求（见 proc.c）
 */
#include "types.h"
#include "defs.h"
#include "proc.h"
#include "execve.h"

static void kernel_init(void *arg)
{
	(void)arg;

	printk(KERN_INFO "init: kernel pid=%d starting\n", myproc()->pid);

	/* 加载 initcode 并 iret 进入 ring3；成功则不返回 */
	kernel_execve(myproc(), "initcode");
	panic("init: kernel_execve initcode failed");
}

void rest_init(void)
{
	struct proc *swapper = &proc_table[0];

	/*
	 * 先创建 init，保证拿到 pid 1；再创建 kthreadd 拿 pid 2。
	 * 二者都由 swapper 拉起（此时 kthreadd_task 尚未置位，走同步创建）。
	 */
	initproc = kthread_create(kernel_init, 0, "init");
	if (!initproc)
		panic("rest_init: create init failed");
	initproc->parent = swapper;
	printk(KERN_INFO "init: created pid=%d, parent=swapper\n", initproc->pid);

	kthreadd_task = kthread_create(kthreadd, 0, "kthreadd");
	if (!kthreadd_task)
		panic("rest_init: create kthreadd failed");
	kthreadd_task->parent = swapper;
	printk(KERN_INFO "kthreadd: created pid=%d, parent=swapper\n",
	       kthreadd_task->pid);
}
