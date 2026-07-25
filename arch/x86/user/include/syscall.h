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
#define SYS_link	9	/* Linux i386 __NR_link */
#define SYS_unlink	10	/* Linux i386 __NR_unlink */
#define SYS_execve	11
#define SYS_chdir	12	/* Linux i386 __NR_chdir */
#define SYS_stat	18	/* Linux i386 __NR_oldstat（与 fstat=28 同代） */
#define SYS_lseek	19	/* Linux i386 __NR_lseek */
#define SYS_getpid	20
#define SYS_mount	21	/* Linux i386 __NR_mount */
#define SYS_umount	22	/* Linux i386 __NR_umount */
#define SYS_fstat	28	/* Linux i386 __NR_oldfstat */
#define SYS_kill	37	/* Linux i386 __NR_kill */
#define SYS_rename	38	/* Linux i386 __NR_rename */
#define SYS_mkdir	39	/* Linux i386 __NR_mkdir */
#define SYS_rmdir	40	/* Linux i386 __NR_rmdir */
#define SYS_dup		41	/* Linux i386 __NR_dup */
#define SYS_pipe	42	/* Linux i386 __NR_pipe */
#define SYS_brk		45	/* Linux i386 __NR_brk */
#define SYS_signal	48	/* Linux i386 __NR_signal */
#define SYS_ioctl	54	/* Linux i386 __NR_ioctl */
#define SYS_fcntl	55	/* Linux i386 __NR_fcntl */
#define SYS_symlink	83	/* Linux i386 __NR_symlink */
#define SYS_readlink	85	/* Linux i386 __NR_readlink */
#define SYS_munmap	91	/* Linux i386 __NR_munmap */
#define SYS_sigreturn	119	/* Linux i386 __NR_sigreturn */
#define SYS_flock	143	/* Linux i386 __NR_flock */
#define SYS_nanosleep	162	/* Linux i386 __NR_nanosleep */
#define SYS_getcwd	183	/* Linux i386 __NR_getcwd */
#define SYS_mmap2	192	/* Linux i386 __NR_mmap2（offset 为页偏移） */
#define SYS_getcpu	318	/* Linux i386 __NR_getcpu */

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
#define S_IFLNK		0120000

#define S_ISFIFO(m)	(((m) & S_IFMT) == S_IFIFO)
#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK)
#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)
#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)

struct stat {
	unsigned short	st_mode;
	unsigned int	st_ino;
	unsigned int	st_size;
};

/* open flags */
#define O_RDONLY	0x000
#define O_WRONLY	0x001
#define O_RDWR		0x002
#define O_ACCMODE	0x003
#define O_CREATE	0x200
#define O_APPEND	0x400	/* Linux O_APPEND */
#define O_NONBLOCK	0x800	/* Linux O_NONBLOCK */

/* fcntl(2) cmd（对齐 Linux） */
#define F_DUPFD		0
#define F_GETFD		1
#define F_SETFD		2
#define F_GETFL		3
#define F_SETFL		4
#define F_DUPFD_CLOEXEC	1030

#define FD_CLOEXEC	1

/* flock(2) operation（对齐 Linux） */
#define LOCK_SH		1	/* 共享锁 */
#define LOCK_EX		2	/* 排他锁 */
#define LOCK_NB		4	/* 非阻塞 */
#define LOCK_UN		8	/* 解锁 */

/* lseek whence（与 Linux <unistd.h> 一致） */
#define SEEK_SET	0
#define SEEK_CUR	1
#define SEEK_END	2

/* mmap prot / flags（与 Linux 一致；本内核先支持匿名 PRIVATE） */
#define PROT_NONE	0x0	/* 无保护 */
#define PROT_READ	0x1	/* 读保护 */
#define PROT_WRITE	0x2	/* 写保护 */
#define PROT_EXEC	0x4	/* 执行保护 */

#define MAP_SHARED	0x01	/* 共享映射 */
#define MAP_PRIVATE	0x02	/* 私有映射 */
#define MAP_FIXED	0x10	/* 固定映射 */
#define MAP_ANONYMOUS	0x20	/* 匿名映射 */

#define MAP_FAILED	((void *)-1)	/* 映射失败 */
#endif

#endif
