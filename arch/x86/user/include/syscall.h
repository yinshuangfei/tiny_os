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
#define SYS_getpid	20
#define SYS_nanosleep	162	/* Linux i386 __NR_nanosleep */

#ifndef __ASSEMBLER__
struct timespec {
	unsigned int tv_sec;
	unsigned int tv_nsec;
};

/* open flags */
#define O_RDONLY	0x000
#define O_WRONLY	0x001
#define O_RDWR		0x002
#define O_CREATE	0x200
#endif

#endif
