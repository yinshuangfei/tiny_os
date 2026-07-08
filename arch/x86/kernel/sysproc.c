/*
 * 进程相关系统调用实现。
 */
#include "types.h"
#include "defs.h"
#include "proc.h"
#include "syscall.h"
#include "execve.h"
#include "x86.h"

int sys_getpid(struct trapframe *tf)
{
	(void)tf;
	return myproc()->pid;
}

int sys_exit(struct trapframe *tf)
{
	int status;

	if (argint(tf, 0, &status) < 0)
		status = -1;
	exit(status);
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

int sys_execve(struct trapframe *tf)
{
	char path[NNAME];
	uint upath;
	struct proc *p = myproc();

	if (!p || !p->pagetable)
		return -1;
	if (argaddr(tf, 0, &upath) < 0)
		return -1;
	if (copyinstr(p->pagetable, path, upath, NNAME) < 0)
		return -1;
	/* envp/argv 暂未实现，忽略 arg1、arg2 */
	return execve(p, tf, path);
}
