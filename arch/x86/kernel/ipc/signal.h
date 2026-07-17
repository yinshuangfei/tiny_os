/*
 * 信号（教学版，语义贴近 Linux 最小子集）。
 * 系统调用号对齐 Linux i386：kill=37, signal=48, sigreturn=119。
 */
#ifndef __IPC_SIGNAL_H__
#define __IPC_SIGNAL_H__

#include "../types.h"
#include "../trap.h"

struct proc;

#define NSIG		32	/* 信号数 */

#define SIGHUP		1	/* 挂起 */
#define SIGINT		2	/* 中断 */
#define SIGQUIT		3	/* 退出 */
#define SIGILL		4	/* 非法指令 */
#define SIGTRAP		5	/* 断点 */
#define SIGABRT		6	/* 异常终止 */
#define SIGBUS		7	/* 总线错误 */
#define SIGFPE		8	/* 浮点异常 */
#define SIGKILL		9	/* 杀死进程 */
#define SIGUSR1		10	/* 用户信号 1 */
#define SIGSEGV		11	/* 段错误 */
#define SIGUSR2		12	/* 用户信号 2 */
#define SIGPIPE		13	/* 管道破裂 */
#define SIGALRM		14	/* 定时器到时 */
#define SIGTERM		15	/* 终止 */
#define SIGCHLD		17	/* 子进程退出 */
#define SIGCONT		18	/* 继续执行 */
#define SIGSTOP		19	/* 停止执行 */

#define SIG_DFL		0u	/* 默认处理函数 */
#define SIG_IGN		1u	/* 忽略处理函数 */

/* 向 pid 发送信号；成功 0，失败 -1 */
int signal_send(int pid, int sig);

/* 子进程退出时通知父进程（SIGCHLD） */
void signal_parent_child_exit(struct proc *child);

/* fork：复制 handler，清空 pending / 处理中标志 */
void signal_fork(struct proc *np, struct proc *p);

/* execve：handler 复位为 SIG_DFL（保留 SIG_IGN） */
void signal_exec_reset(struct proc *p);

/* exit：释放 sigold 等 */
void signal_exit_cleanup(struct proc *p);

/*
 * 即将 iret 回 ring3 前调用：按 pending 执行默认动作或切换到用户 handler。
 * 可能调用 exit() 而不返回。
 */
void signal_notify(struct trapframe *tf);

int sys_kill(struct trapframe *tf);
int sys_signal(struct trapframe *tf);
int sys_sigreturn(struct trapframe *tf);

#endif
