/*
 * 信号实现：pending 位图 + 每信号 handler（SIG_DFL / SIG_IGN / 用户地址）。
 * 投递点：trap 返回用户态前 signal_notify()（类似 Linux do_signal）。
 *
 * 被中断的 trapframe 保存在 kmalloc 的 sigold 中（不塞进 PCB，避免撑大进程表）。
 */
#include "../types.h"
#include "../defs.h"
#include "../param.h"
#include "../proc.h"
#include "../syscall.h"
#include "../mm/memlayout.h"
#include "../mm/vm.h"
#include "../mm/mmu.h"
#include "../gdt.h"
#include "../lock/proc_lock.h"
#include "../task_queue.h"
#include "signal.h"

#define SIGBIT(sig)	(1u << ((sig) - 1))

static int valid_sig(int sig)
{
	return sig >= 1 && sig < NSIG;
}

/* 判断信号是否是致命的默认动作 */
static int sig_fatal_default(int sig)
{
	/* SIGCHLD / SIGCONT 默认忽略；其余默认终止 */
	if (sig == SIGCHLD || sig == SIGCONT)
		return 0;
	return 1;
}

/* 释放 sigold */
static void signal_free_old(struct proc *p)
{
	if (p->sigold) {
		kfree(p->sigold);
		p->sigold = 0;
	}
}

/* 唤醒因信号而应中断的睡眠进程 */
static void signal_wake_locked(struct proc *p)
{
	if (p->state == SLEEPING) {
		p->state = RUNNABLE;
		p->chan = 0;
		p->wakeup_tick = 0;
		task_queue_enqueue_locked(p);
	}
}

/* 向 pid 发送信号 */
int signal_send(int pid, int sig)
{
	struct proc *p;
	int i;

	if (!valid_sig(sig))
		return -1;

	acquire(&proc_lock);
	p = 0;
	for (i = 0; i < NPROC; i++) {
		if (proc_table[i].pid == pid &&
		    proc_table[i].state != UNUSED &&
		    proc_table[i].state != ZOMBIE) {
			p = &proc_table[i];
			break;
		}
	}
	if (!p || !p->pagetable) {
		release(&proc_lock);
		return -1;
	}

	if (sig == SIGKILL) {
		p->sighand[sig] = SIG_DFL;
		p->sigmasked &= ~SIGBIT(sig);
	}

	p->sigpending |= SIGBIT(sig);
	signal_wake_locked(p);
	release(&proc_lock);
	return 0;
}

/* 子进程退出时通知父进程（SIGCHLD） */
void signal_parent_child_exit(struct proc *child)
{
	struct proc *parent;

	if (!child)
		return;
	parent = child->parent;
	if (!parent || parent == child)
		return;
	if (!parent->pagetable)
		return;

	/* 仅置 pending；唤醒由 exit() 随后的 wakeup(parent) 完成 */
	acquire(&proc_lock);
	parent->sigpending |= SIGBIT(SIGCHLD);
	release(&proc_lock);
}

/* fork 时复制信号处理函数 */
void signal_fork(struct proc *np, struct proc *p)
{
	int i;

	if (!np || !p)
		return;
	for (i = 0; i < NSIG; i++)
		np->sighand[i] = p->sighand[i];
	np->sigpending = 0;
	np->sigmasked = 0;
	np->sighandling = 0;
	np->sigold = 0;
}

/* 执行 execve 时重置信号处理函数 */
void signal_exec_reset(struct proc *p)
{
	int i;

	if (!p)
		return;
	for (i = 0; i < NSIG; i++) {
		if (p->sighand[i] != SIG_IGN)
			p->sighand[i] = SIG_DFL;
	}
	p->sigpending = 0;
	p->sigmasked = 0;
	p->sighandling = 0;
	signal_free_old(p);
}

/* 退出时释放 sigold */
void signal_exit_cleanup(struct proc *p)
{
	if (!p)
		return;
	p->sighandling = 0;
	signal_free_old(p);
}

/*
 * 用户栈 handler 帧 + sigreturn 蹦床：
 *   movl $SYS_sigreturn, %eax ;
 *   int $0x80
 */
static int signal_setup_frame(struct proc *p, struct trapframe *tf, int sig,
			      uint handler)
{
	uint sp, retaddr, signum;
	uchar tramp[8];
	struct trapframe *old;

	if (!p->pagetable || !(tf->cs & DPL_USER))
		return -1;

	sp = tf->esp;
	if (sp < USERBASE + 64 || sp >= USERSTACK)
		return -1;
	if (handler < USERBASE || handler >= USEREND)
		return -1;

	old = kmalloc(sizeof(*old));
	if (!old)
		return -1;

	/* 设置跳板代码 */
	tramp[0] = 0xb8;
	tramp[1] = (uchar)(SYS_sigreturn & 0xff);
	tramp[2] = (uchar)((SYS_sigreturn >> 8) & 0xff);
	tramp[3] = (uchar)((SYS_sigreturn >> 16) & 0xff);
	tramp[4] = (uchar)((SYS_sigreturn >> 24) & 0xff);
	tramp[5] = 0xcd;
	tramp[6] = 0x80;
	tramp[7] = 0x90;

	/* 将跳板代码复制到用户栈 */
	sp -= sizeof(tramp);
	sp &= ~0x3;	/* 对齐 */
	if (copyout(p->pagetable, sp, tramp, sizeof(tramp)) < 0)
		goto bad;
	retaddr = sp;

	/* 将信号编号复制到用户栈 */
	signum = (uint)sig;
	sp -= sizeof(uint);
	if (copyout(p->pagetable, sp, &signum, sizeof(uint)) < 0)
		goto bad;

	/* 将返回地址复制到用户栈 */
	sp -= sizeof(uint);
	if (copyout(p->pagetable, sp, &retaddr, sizeof(uint)) < 0)
		goto bad;

	*old = *tf;
	signal_free_old(p);
	p->sigold = old;
	p->sighandling = 1;
	tf->esp = sp;
	tf->eip = handler;
	return 0;

bad:
	kfree(old);
	return -1;
}

/* 从未决信号集中获取一个信号 */
static int signal_dequeue(struct proc *p)
{
	uint bits;
	int sig;

	/* 获取未决信号集，并排除阻塞信号集 */
	bits = p->sigpending & ~p->sigmasked;
	if (bits == 0)
		return 0;

	/* 遍历未决信号集，找到第一个未决信号 */
	for (sig = 1; sig < NSIG; sig++) {
		if (bits & SIGBIT(sig)) {
			p->sigpending &= ~SIGBIT(sig);
			return sig;
		}
	}
	return 0;
}

/* 处理信号 */
void signal_notify(struct trapframe *tf)
{
	struct proc *p = myproc();
	int sig;
	uint handler;

	if (!p || !tf || !p->pagetable)
		return;
	if ((tf->cs & 3) != DPL_USER)
		return;

	/* 如果正在处理信号，直接返回 */
	if (p->sighandling) {
		/* 处理 SIGKILL 信号，直接退出进程 */
		if (p->sigpending & SIGBIT(SIGKILL)) {
			p->sigpending &= ~SIGBIT(SIGKILL);
			exit_signal(SIGKILL);
		}
		return;
	}

	/* 处理未决信号 */
	for (;;) {
		sig = signal_dequeue(p);
		if (sig == 0)
			return;

		if (sig == SIGKILL)
			exit_signal(SIGKILL);

		handler = p->sighand[sig];
		if (handler == SIG_IGN)
			continue;
		if (handler == SIG_DFL) {
			if (!sig_fatal_default(sig))
				continue;
			exit_signal(sig);
		}

		/* 设置信号处理帧，并设置返回地址为信号处理函数 */
		if (signal_setup_frame(p, tf, sig, handler) < 0)
			exit_signal(sig);
		return;
	}
}

int sys_kill(struct trapframe *tf)
{
	int pid, sig;

	if (argint(tf, 0, &pid) < 0 || argint(tf, 1, &sig) < 0)
		return -1;
	return signal_send(pid, sig);
}

/* 设置信号处理函数 */
int sys_signal(struct trapframe *tf)
{
	struct proc *p = myproc();
	int sig;
	uint handler, old;

	if (!p)
		return -1;
	if (argint(tf, 0, &sig) < 0 || argaddr(tf, 1, &handler) < 0)
		return -1;
	if (!valid_sig(sig) || sig == SIGKILL || sig == SIGSTOP)
		return -1;

	if (handler != SIG_DFL && handler != SIG_IGN &&
	    (handler < USERBASE || handler >= USEREND))
		return -1;

	old = p->sighand[sig];
	p->sighand[sig] = handler;
	return (int)old;
}

/*
 * 恢复进入 handler 前的 trapframe（含原 eax）。
 * 调用方（syscall）不得再写入 tf->eax。
 */
int sys_sigreturn(struct trapframe *tf)
{
	struct proc *p = myproc();

	if (!p || !p->sighandling || !p->sigold)
		return -1;
	*tf = *p->sigold;
	p->sighandling = 0;
	signal_free_old(p);
	return (int)tf->eax;
}
