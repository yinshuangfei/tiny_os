/*
 * 用户态系统调用桩（int 0x80，cdecl 参数从栈加载）。
 * 与 kernel/syscall.c 分发表、user/include/syscall.h 号表一致。
 *
 * 约定：参数进 ebx/ecx/edx/esi/edi，eax = 系统调用号，再 int $0x80。
 * ebx/esi/edi 为 callee-saved（i386 SysV），桩内须保存恢复，否则像 GCC
 * 用 ebx 跨调用保存 &argc 时会被 write(1,...) 破坏（表现为访问 va=0x1）。

 pushl %ebx 之后：
  (%esp)   = 保存的旧 ebx
  4(%esp)  = 返回地址
  8(%esp)  = argv0        ← 所以改成 8(%esp)
  12(%esp) = argv1
  16(%esp) = argv2
 */
#include "syscall.h"

.code32
.section .text

.globl exit
exit:
	pushl %ebx
	movl 8(%esp), %ebx
	movl $SYS_exit, %eax
	int $0x80
	popl %ebx
	ret

.globl execve
execve:
	pushl %ebx
	movl 8(%esp), %ebx
	movl 12(%esp), %ecx
	movl 16(%esp), %edx
	movl $SYS_execve, %eax
	int $0x80
	popl %ebx
	ret

.globl getpid
getpid:
	movl $SYS_getpid, %eax
	int $0x80
	ret

.globl write
write:
	pushl %ebx
	movl 8(%esp), %ebx
	movl 12(%esp), %ecx
	movl 16(%esp), %edx
	movl $SYS_write, %eax
	int $0x80
	popl %ebx
	ret

.globl read
read:
	pushl %ebx
	movl 8(%esp), %ebx
	movl 12(%esp), %ecx
	movl 16(%esp), %edx
	movl $SYS_read, %eax
	int $0x80
	popl %ebx
	ret

.globl nanosleep
nanosleep:
	pushl %ebx
	movl 8(%esp), %ebx
	movl 12(%esp), %ecx
	movl $SYS_nanosleep, %eax
	int $0x80
	popl %ebx
	ret

.globl fork
fork:
	movl $SYS_fork, %eax
	int $0x80
	ret

.globl waitpid
waitpid:
	pushl %ebx
	movl 8(%esp), %ebx
	movl 12(%esp), %ecx
	movl 16(%esp), %edx
	movl $SYS_waitpid, %eax
	int $0x80
	popl %ebx
	ret

.globl open
open:
	pushl %ebx
	movl 8(%esp), %ebx
	movl 12(%esp), %ecx
	movl $SYS_open, %eax
	int $0x80
	popl %ebx
	ret

.globl close
close:
	pushl %ebx
	movl 8(%esp), %ebx
	movl $SYS_close, %eax
	int $0x80
	popl %ebx
	ret

.globl dup
dup:
	pushl %ebx
	movl 8(%esp), %ebx
	movl $SYS_dup, %eax
	int $0x80
	popl %ebx
	ret

.globl pipe
pipe:
	pushl %ebx
	movl 8(%esp), %ebx
	movl $SYS_pipe, %eax
	int $0x80
	popl %ebx
	ret

.globl lseek
lseek:
	pushl %ebx
	movl 8(%esp), %ebx
	movl 12(%esp), %ecx
	movl 16(%esp), %edx
	movl $SYS_lseek, %eax
	int $0x80
	popl %ebx
	ret

.globl fstat
fstat:
	pushl %ebx
	movl 8(%esp), %ebx
	movl 12(%esp), %ecx
	movl $SYS_fstat, %eax
	int $0x80
	popl %ebx
	ret

.globl stat
stat:
	pushl %ebx
	movl 8(%esp), %ebx
	movl 12(%esp), %ecx
	movl $SYS_stat, %eax
	int $0x80
	popl %ebx
	ret

.globl chdir
chdir:
	pushl %ebx
	movl 8(%esp), %ebx
	movl $SYS_chdir, %eax
	int $0x80
	popl %ebx
	ret

.globl getcwd
getcwd:
	pushl %ebx
	movl 8(%esp), %ebx
	movl 12(%esp), %ecx
	movl $SYS_getcwd, %eax
	int $0x80
	popl %ebx
	ret

.globl mkdir
mkdir:
	pushl %ebx
	movl 8(%esp), %ebx
	movl 12(%esp), %ecx
	movl $SYS_mkdir, %eax
	int $0x80
	popl %ebx
	ret

.globl rmdir
rmdir:
	pushl %ebx
	movl 8(%esp), %ebx
	movl $SYS_rmdir, %eax
	int $0x80
	popl %ebx
	ret

.globl kill
kill:
	pushl %ebx
	movl 8(%esp), %ebx
	movl 12(%esp), %ecx
	movl $SYS_kill, %eax
	int $0x80
	popl %ebx
	ret

.globl signal
signal:
	pushl %ebx
	movl 8(%esp), %ebx
	movl 12(%esp), %ecx
	movl $SYS_signal, %eax
	int $0x80
	popl %ebx
	ret

.globl sigreturn
sigreturn:
	movl $SYS_sigreturn, %eax
	int $0x80
	ret
