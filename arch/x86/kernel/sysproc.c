/*
 * 进程相关系统调用实现。
 */
#include "types.h"
#include "defs.h"
#include "param.h"
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

/*
 * 从用户地址空间拷贝以 NULL 结尾的字符串指针表到内核。
 * store 容量为 max * NNAME；ptrs[] 填入指向 store 的指针，以 NULL 结尾。
 * 成功返回个数，失败 -1（调用方负责 kfree(store)）。
 */
static int copy_user_strvec(pagetable_t pgdir, uint uvec, char *store, int max,
			    char **ptrs)
{
	uint uarg;
	int n;

	n = 0;
	if (uvec == 0) {
		ptrs[0] = 0;
		return 0;
	}
	for (;;) {
		if (n >= max)
			return -1;
		if (copyin(pgdir, &uarg, uvec + (uint)n * sizeof(uint),
			   sizeof(uint)) < 0)
			return -1;
		if (uarg == 0)
			break;
		if (copyinstr(pgdir, store + n * NNAME, uarg, NNAME) < 0)
			return -1;
		ptrs[n] = store + n * NNAME;
		n++;
	}
	ptrs[n] = 0;
	return n;
}

int sys_execve(struct trapframe *tf)
{
	char path[NNAME];
	char *argstore, *envstore;
	char *kargv[MAXARG + 1];
	char *kenvp[MAXENV + 1];
	uint upath, uargv, uenvp;
	struct proc *p = myproc();
	int argc, envc, r;

	if (!p || !p->pagetable)
		return -1;
	if (argaddr(tf, 0, &upath) < 0)
		return -1;
	if (copyinstr(p->pagetable, path, upath, NNAME) < 0)
		return -1;

	/* 临时申请的存储空间 */
	argstore = 0;
	envstore = 0;
	argc = 0;
	envc = 0;

	/* argv 和 envp 指针数组 */
	kargv[0] = 0;
	kenvp[0] = 0;

	/* 换页表前从旧地址空间拷出 argv / envp（Linux execve 三参数） */
	if (argaddr(tf, 1, &uargv) == 0 && uargv != 0) {
		argstore = kmalloc(MAXARG * NNAME);
		if (!argstore)
			return -1;
		argc = copy_user_strvec(p->pagetable, uargv, argstore, MAXARG,
					kargv);
		if (argc < 0) {
			kfree(argstore);
			return -1;
		}
	}
	if (argaddr(tf, 2, &uenvp) == 0 && uenvp != 0) {
		envstore = kmalloc(MAXENV * NNAME);
		if (!envstore) {
			if (argstore)
				kfree(argstore);
			return -1;
		}
		envc = copy_user_strvec(p->pagetable, uenvp, envstore, MAXENV,
					kenvp);
		if (envc < 0) {
			if (argstore)
				kfree(argstore);
			kfree(envstore);
			return -1;
		}
	}

	r = execve(p, tf, path, argc ? kargv : 0, envc ? kenvp : 0);
	if (argstore)
		kfree(argstore);
	if (envstore)
		kfree(envstore);
	return r;
}
