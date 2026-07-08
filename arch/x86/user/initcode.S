/*
 * 第一个用户态程序：触发 int 0x80 后自旋。
 * 链接地址须与 memlayout.h 中 USERBASE 一致。
 */
.code32
.section .text
.global start
start:
	movl $42, %eax
	int $0x80
1:	jmp 1b
