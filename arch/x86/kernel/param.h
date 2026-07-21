#ifndef __PARAM_H__
#define __PARAM_H__

#define NPROC        64		/* 最大进程数 */

/*
 * NR_CPUS ≈ Linux CONFIG_NR_CPUS / NR_CPUS：
 * per-CPU 数组与 GDT TSS 的编译期上界。
 * 可用 make NR_CPUS=8 覆盖；须 >= CPUS（Makefile 会检查）。
 */
#ifndef NR_CPUS
#define NR_CPUS         256
#endif

#define NOFILE       256	/* 每进程打开文件数上限（预留） */
#define NNAME        256	/* 进程名 / 单参数字符串长度上限 */
#define MAXARG       32		/* execve argv 最大参数个数（含 argv[0]） */
#define MAXENV       32		/* execve envp 最大环境变量个数 */
#define NVMA         64		/* 每进程 mmap 区域数上限（教学） */

#endif
