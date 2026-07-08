/*
 * 引导用户程序：execve("initcode")，演示 execve 系统调用。
 * 由 start_first_user() 加载；成功后不再返回。
 */
.include "user/syscall.inc"

.code32
.section .rodata
path:
	.string "initcode"

.section .text
.global start
start:
	/* execve(path, 0, 0) */
	movl $SYS_execve, %eax
	movl $path, %ebx
	xorl %ecx, %ecx
	xorl %edx, %edx
	int $0x80

	/* execve 失败 */
	movl $SYS_exit, %eax
	movl $1, %ebx
	int $0x80

1:	jmp 1b
