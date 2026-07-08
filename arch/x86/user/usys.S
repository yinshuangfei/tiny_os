/*
 * 用户态系统调用桩（int 0x80）。
 * 与 kernel/syscall.c 分发表、user/syscall.h 号表一致。
 *
 * 用法（用户 C 程序链接本文件）：
 *   extern int write(int, const void *, int);
 *   write(1, "hi\n", 3);
 */
.include "user/syscall.inc"

#define SYSCALL(name, num) \
	.globl name; \
name: \
	movl $num, %eax; \
	int $0x80; \
	ret

.section .text

SYSCALL(exit,   SYS_exit)
SYSCALL(execve, SYS_execve)
SYSCALL(getpid, SYS_getpid)
SYSCALL(write,  SYS_write)
