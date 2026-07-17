/*
 * 信号常量与类型（用户 / 内核共享语义；号值对齐 Linux）。
 */
#ifndef __USER_SIGNAL_H__
#define __USER_SIGNAL_H__

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

#define SIG_DFL		((void (*)(int))0)	/* 默认处理函数 */
#define SIG_IGN		((void (*)(int))1)	/* 忽略处理函数 */

#ifndef __ASSEMBLER__
typedef void (*sighandler_t)(int);
#endif

#endif
