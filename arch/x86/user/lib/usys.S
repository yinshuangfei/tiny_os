/*
 * 用户态系统调用桩（int 0x80，cdecl 参数从栈加载）。
 * 与 kernel/syscall.c 分发表、user/include/syscall.h 号表一致。
 * 主要作用：把栈参数搬进 int 0x80 约定寄存器: ebx, ecx, edx, esi, edi;
 *          然后把 eax 设为系统调用号，再 int 0x80 触发中断，进入内核。
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

.globl read
read:
	movl 4(%esp), %ebx
	movl 8(%esp), %ecx
	movl 12(%esp), %edx
	movl $SYS_read, %eax
	int $0x80
	ret

.globl nanosleep
nanosleep:
	movl 4(%esp), %ebx
	movl 8(%esp), %ecx
	movl $SYS_nanosleep, %eax
	int $0x80
	ret

.globl fork
fork:
	movl $SYS_fork, %eax
	int $0x80
	ret

.globl waitpid
waitpid:
	movl 4(%esp), %ebx
	movl 8(%esp), %ecx
	movl 12(%esp), %edx
	movl $SYS_waitpid, %eax
	int $0x80
	ret

.globl open
open:
	movl 4(%esp), %ebx
	movl 8(%esp), %ecx
	movl $SYS_open, %eax
	int $0x80
	ret

.globl close
close:
	movl 4(%esp), %ebx
	movl $SYS_close, %eax
	int $0x80
	ret
