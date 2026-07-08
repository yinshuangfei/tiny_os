/*
 * 第一个用户态程序：演示 write / getpid / exit 系统调用。
 * 链接地址须与 memlayout.h 中 USERBASE 一致。
 */
.include "user/syscall.inc"

.code32
.section .rodata
msg:
	.string "hello from user\n"

.section .text
.global start
start:
	/* write(1, msg, 17) */
	movl $SYS_write, %eax
	movl $1, %ebx
	movl $msg, %ecx
	movl $17, %edx
	int $0x80

	/* getpid() */
	movl $SYS_getpid, %eax
	int $0x80

	/* exit(0) */
	movl $SYS_exit, %eax
	xorl %ebx, %ebx
	int $0x80

1:	jmp 1b
