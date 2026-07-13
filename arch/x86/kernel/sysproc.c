/*
 * 进程相关系统调用实现。
 */
#include "types.h"
#include "defs.h"
#include "proc.h"
#include "syscall.h"
#include "execve.h"
#include "timer.h"
#include "x86.h"

#define NSEC_PER_SEC  1000000000u
#define NSEC_PER_TICK (NSEC_PER_SEC / TIMER_HZ)

static unsigned int timespec_to_ticks(const struct timespec *ts)
{
	uint64 ticks;

	if (ts->tv_sec == 0 && ts->tv_nsec == 0)
		return 0;

	/* 分 sec/nsec 换算，避免 (tv_sec * 1e9) 在 32 位下溢出 */
	ticks = (uint64)ts->tv_sec * TIMER_HZ;
	ticks += ((uint64)ts->tv_nsec + NSEC_PER_TICK - 1) / NSEC_PER_TICK;
	if (ticks == 0)
		ticks = 1;
	if (ticks > 0xffffffffu)
		return 0xffffffffu;
	return (unsigned int)ticks;
}

int sys_getpid(struct trapframe *tf)
{
	(void)tf;
	return myproc()->pid;
}

int sys_fork(struct trapframe *tf)
{
	return fork_copy(tf);
}

int sys_waitpid(struct trapframe *tf)
{
	int pid, options, st, reaped;
	uint ustatus;
	struct proc *p = myproc();

	if (!p || !p->pagetable)
		return -1;
	if (argint(tf, 0, &pid) < 0)
		return -1;
	if (argint(tf, 2, &options) < 0)
		options = 0;
	(void)options;

	reaped = wait_child(pid, &st, p);
	if (reaped < 0)
		return -1;

	if (argaddr(tf, 1, &ustatus) == 0 && ustatus != 0) {
		if (copyout(p->pagetable, ustatus, &st, sizeof(st)) < 0)
			return -1;
	}
	return reaped;
}

int sys_exit(struct trapframe *tf)
{
	int status;

	if (argint(tf, 0, &status) < 0)
		status = -1;
	exit(status);
}

int sys_nanosleep(struct trapframe *tf)
{
	struct proc *p = myproc();
	struct timespec req, rem;
	uint ureq, urem;
	unsigned int nticks;

	if (!p || !p->pagetable)
		return -1;
	if (argaddr(tf, 0, &ureq) < 0 || ureq == 0)
		return -1;
	if (copyin(p->pagetable, &req, ureq, sizeof(req)) < 0)
		return -1;
	if (req.tv_nsec >= NSEC_PER_SEC)
		return -1;

	nticks = timespec_to_ticks(&req);
	if (nticks > 0) {
		unsigned int now = timer_ticks();

		sleep_deadline(now + nticks);
	}

	if (argaddr(tf, 1, &urem) == 0 && urem != 0) {
		rem.tv_sec = 0;
		rem.tv_nsec = 0;
		if (copyout(p->pagetable, urem, &rem, sizeof(rem)) < 0)
			return -1;
	}
	return 0;
}

int sys_read(struct trapframe *tf)
{
	int fd, n, i;
	uint uaddr;
	struct proc *p = myproc();
	char c;

	if (argint(tf, 0, &fd) < 0 || argaddr(tf, 1, &uaddr) < 0 ||
	    argint(tf, 2, &n) < 0)
		return -1;
	if (fd != 0 || n < 0)
		return -1;
	if (!p || !p->pagetable)
		return -1;

	for (i = 0; i < n; i++) {
		c = (char)uart_getc();
		if (copyout(p->pagetable, uaddr + i, &c, 1) < 0)
			return -1;
	}
	return n;
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
