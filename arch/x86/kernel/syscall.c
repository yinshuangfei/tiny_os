/*
 * 系统调用分发与参数提取。
 *
 * x86 int 0x80 约定（与 Linux 32 位传统接口一致）：
 *   eax = 系统调用号
 *   ebx = arg0, ecx = arg1, edx = arg2, esi = arg3, edi = arg4, ebp = arg5
 *   返回值写入 eax
 */
#include "types.h"
#include "defs.h"
#include "proc.h"
#include "syscall.h"

#define NELEM(x)	((int)(sizeof(x) / sizeof((x)[0])))

static int argraw(struct trapframe *tf, int n)
{
	switch (n) {
	case 0:
		return tf->ebx;
	case 1:
		return tf->ecx;
	case 2:
		return tf->edx;
	case 3:
		return tf->esi;
	case 4:
		return tf->edi;
	case 5:
		return tf->ebp;
	default:
		panic("argraw");
		return 0;
	}
}

int argint(struct trapframe *tf, int n, int *ip)
{
	*ip = argraw(tf, n);
	return 0;
}

int argaddr(struct trapframe *tf, int n, uint *ip)
{
	*ip = (uint)argraw(tf, n);
	return 0;
}

int argstr(struct trapframe *tf, int n, char *buf, int max)
{
	uint addr;

	if (argaddr(tf, n, &addr) < 0)
		return -1;
	if (copyinstr(myproc()->pagetable, buf, addr, max) < 0)
		return -1;
	return 0;
}

/**
 * syscalls[]：这是一个数组，名字叫做 syscalls。
 * (*syscalls[])：括号和星号表示数组里的每一个元素都是一个指针。
 * (*syscalls[])(struct trapframe *tf)：指针后面的 (struct trapframe *tf) 表示这些
 * 	指针指向的是函数，且这些函数接受一个 struct trapframe *tf 参数。
 */
static int (*syscalls[])(struct trapframe *tf) = {
	[SYS_exit] = sys_exit,
	[SYS_execve] = sys_execve,
	[SYS_waitpid] = sys_waitpid,
	[SYS_getpid] = sys_getpid,
	[SYS_write] = sys_write,
	[SYS_fork] = sys_fork,
	[SYS_read] = sys_read,
	[SYS_open] = sys_open,
	[SYS_close] = sys_close,
	[SYS_fstat] = sys_fstat,
	[SYS_stat] = sys_stat,
	[SYS_readlink] = sys_readlink,
	[SYS_lseek] = sys_lseek,
	[SYS_chdir] = sys_chdir,
	[SYS_getcwd] = sys_getcwd,
	[SYS_mkdir] = sys_mkdir,
	[SYS_rmdir] = sys_rmdir,
	[SYS_link] = sys_link,
	[SYS_symlink] = sys_symlink,
	[SYS_mount] = sys_mount,
	[SYS_umount] = sys_umount,
	[SYS_unlink] = sys_unlink,
	[SYS_rename] = sys_rename,
	[SYS_dup] = sys_dup,
	[SYS_pipe] = sys_pipe,
	[SYS_brk] = sys_brk,
	[SYS_mmap2] = sys_mmap2,
	[SYS_munmap] = sys_munmap,
	[SYS_ioctl] = sys_ioctl,
	[SYS_nanosleep] = sys_nanosleep,
	[SYS_kill] = sys_kill,
	[SYS_signal] = sys_signal,
	[SYS_sigreturn] = sys_sigreturn,
	[SYS_getcpu] = sys_getcpu,
};

void syscall(struct trapframe *tf)
{
	int num;
	struct proc *p = myproc();
	int ret;

	if (p)
		p->kframe = tf;

	num = tf->eax;
	if (num > 0 && num < NELEM(syscalls) && syscalls[num]) {
		ret = syscalls[num](tf);
		/* sigreturn 已整帧恢复（含原 eax），勿再覆盖 */
		if (num != SYS_sigreturn)
			tf->eax = ret;
		return;
	}

	printf("syscall: pid=%d unknown sys call %d\n",
	       p ? p->pid : -1, num);
	tf->eax = -1;
}
