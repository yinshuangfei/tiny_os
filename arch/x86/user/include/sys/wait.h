/*
 * wait 状态字宏（对齐 Linux <sys/wait.h> / bits/waitstatus.h 教学子集）。
 *
 * 状态字布局（int）：
 *   正常退出：  (exit_code & 0xff) << 8
 *   信号终止：  (sig & 0x7f)[ | 0x80 若 core]
 *   停止：      (sig << 8) | 0x7f
 */
#ifndef __USER_SYS_WAIT_H__
#define __USER_SYS_WAIT_H__

/* waitpid options（本内核暂可忽略，宏先对齐） */
#define WNOHANG		1
#define WUNTRACED	2
#define WCONTINUED	8

#define __WEXITSTATUS(status)	(((status) & 0xff00) >> 8)
#define __WTERMSIG(status)	((status) & 0x7f)
#define __WSTOPSIG(status)	__WEXITSTATUS(status)
#define __WIFEXITED(status)	(__WTERMSIG(status) == 0)
#define __WIFSIGNALED(status) \
	(((signed char)(((status) & 0x7f) + 1) >> 1) > 0)
#define __WIFSTOPPED(status)	(((status) & 0xff) == 0x7f)
#define __WIFCONTINUED(status)	((status) == 0xffff)
#define __WCOREDUMP(status)	((status) & 0x80)

#define __W_EXITCODE(ret, sig)	(((ret) << 8) | (sig))
#define __W_STOPCODE(sig)	(((sig) << 8) | 0x7f)

/* wexitstatus: 进程正常退出时的退出状态, Wait Exit Status */
#define WEXITSTATUS(status)	__WEXITSTATUS(status)
/* wtermsig: 进程被信号终止时的信号, Wait Term Signal */
#define WTERMSIG(status)	__WTERMSIG(status)
/* wstopsig: 进程被停止时的信号, Wait Stop Signal */
#define WSTOPSIG(status)	__WSTOPSIG(status)
/* wifexited: 进程正常退出, Wait If Exited */
#define WIFEXITED(status)	__WIFEXITED(status)
/* wifsignaled: 进程被信号终止, Wait If Signaled */
#define WIFSIGNALED(status)	__WIFSIGNALED(status)
/* wifstopped: 进程被停止, Wait If Stopped */
#define WIFSTOPPED(status)	__WIFSTOPPED(status)
/* wifcontinued: 进程被继续, Wait If Continued */
#define WIFCONTINUED(status)	__WIFCONTINUED(status)
/* wcore dumped: 进程产生核心转储, Wait Core Dumped */
#define WCOREDUMP(status)	__WCOREDUMP(status)

#endif
