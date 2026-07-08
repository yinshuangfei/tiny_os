/*
 * 用户态系统调用桩（int 0x80，cdecl 参数从栈加载）。
 * 与 kernel/syscall.c 分发表、user/syscall.h 号表一致。
 */
#include "syscall.h"

.code32
.section .text

.globl exit
exit:
	movl 4(%esp), %ebx
	movl $SYS_exit, %eax
	int $0x80
	ret

.globl execve
execve:
	movl 4(%esp), %ebx
	movl 8(%esp), %ecx
	movl 12(%esp), %edx
	movl $SYS_execve, %eax
	int $0x80
	ret

.globl getpid
getpid:
	movl $SYS_getpid, %eax
	int $0x80
	ret

.globl write
write:
	movl 4(%esp), %ebx
	movl 8(%esp), %ecx
	movl 12(%esp), %edx
	movl $SYS_write, %eax
	int $0x80
	ret
