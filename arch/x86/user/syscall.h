/*
 * 系统调用号（内核 / 用户态共享）。
 * int 0x80 约定：eax=号，ebx..ebp=参数 0..5，返回值在 eax。
 */
#ifndef __USER_SYSCALL_H__
#define __USER_SYSCALL_H__

#define SYS_exit	2
#define SYS_execve	3
#define SYS_getpid	11
#define SYS_write	16

#endif
