/*
 * 进程相关系统调用实现。
 */
#include "types.h"
#include "defs.h"
#include "proc.h"
#include "syscall.h"
#include "x86.h"

int sys_getpid(struct trapframe *tf)
{
	(void)tf;
	return myproc()->pid;
}

int sys_exit(struct trapframe *tf)
{
	int status;

	argint(tf, 0, &status);
	printf("sys_exit: pid=%d status=%d\n", myproc()->pid, status);
	/* 首个用户进程尚无完整回收路径，挂起 CPU */
	for (;;)
		halt();
}

int sys_write(struct trapframe *tf)
{
	int fd, n, i;
	uint uaddr;
	struct proc *p = myproc();
	char buf[128];

	if (argint(tf, 0, &fd) < 0 || argaddr(tf, 1, &uaddr) < 0 ||
	    argint(tf, 2, &n) < 0)
		return -1;
	if (fd != 1 || n < 0)
		return -1;
	if (n > (int)sizeof(buf))
		n = sizeof(buf);
	if (!p || !p->pagetable)
		return -1;
	if (copyin(p->pagetable, buf, uaddr, n) < 0)
		return -1;
	for (i = 0; i < n; i++)
		uart_putc(buf[i]);
	return n;
}
