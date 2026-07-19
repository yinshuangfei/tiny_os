/*
 * 系统调用号（内核 / 用户态共享）。
 * int 0x80 约定：eax=号，ebx..ebp=参数 0..5，返回值在 eax。
 */
#ifndef __USER_SYSCALL_H__
#define __USER_SYSCALL_H__

#define SYS_exit	1
#define SYS_fork	2
#define SYS_read	3
#define SYS_write	4
#define SYS_open	5
#define SYS_close	6
#define SYS_waitpid	7
#define SYS_execve	11
#define SYS_chdir	12	/* Linux i386 __NR_chdir */
#define SYS_stat	18	/* Linux i386 __NR_oldstat（与 fstat=28 同代） */
#define SYS_lseek	19	/* Linux i386 __NR_lseek */
#define SYS_getpid	20
#define SYS_fstat	28	/* Linux i386 __NR_oldfstat */
#define SYS_kill	37	/* Linux i386 __NR_kill */
#define SYS_mkdir	39	/* Linux i386 __NR_mkdir */
#define SYS_rmdir	40	/* Linux i386 __NR_rmdir */
#define SYS_dup		41	/* Linux i386 __NR_dup */
#define SYS_pipe	42	/* Linux i386 __NR_pipe */
#define SYS_signal	48	/* Linux i386 __NR_signal */
#define SYS_sigreturn	119	/* Linux i386 __NR_sigreturn */
#define SYS_nanosleep	162	/* Linux i386 __NR_nanosleep */
#define SYS_getcwd	183	/* Linux i386 __NR_getcwd */

#ifndef __ASSEMBLER__
struct timespec {
	unsigned int tv_sec;
	unsigned int tv_nsec;
};

/*
 * fstat 结果（字段名对齐 Linux struct stat 常用子集）。
 * st_mode 使用 S_IF* 文件类型位。
 */
#define S_IFMT		00170000
#define S_IFIFO		0010000
#define S_IFDIR		0040000
#define S_IFCHR		0020000
#define S_IFBLK		0060000
#define S_IFREG		0100000

#define S_ISFIFO(m)	(((m) & S_IFMT) == S_IFIFO)
#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK)
#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)

struct stat {
	unsigned short	st_mode;
	unsigned int	st_ino;
	unsigned int	st_size;
};

/* open flags */
#define O_RDONLY	0x000
#define O_WRONLY	0x001
#define O_RDWR		0x002
#define O_CREATE	0x200

/* lseek whence（与 Linux <unistd.h> 一致） */
#define SEEK_SET	0
#define SEEK_CUR	1
#define SEEK_END	2
#endif

#endif
