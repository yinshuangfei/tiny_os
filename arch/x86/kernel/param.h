#ifndef __PARAM_H__
#define __PARAM_H__

#define NPROC        64		/* 最大进程数 */
#define NCPU         1		/* 单核，SMP 未启用，预留 */
#define NOFILE       256	/* 每进程打开文件数上限（预留） */
#define NNAME        256	/* 进程名 / 单参数字符串长度上限 */
#define MAXARG       32		/* execve argv 最大参数个数（含 argv[0]） */
#define MAXENV       32		/* execve envp 最大环境变量个数 */

#endif
