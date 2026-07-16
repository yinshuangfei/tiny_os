/*
 * 用户 C 程序入口（对齐 Linux i386 crt1 / SysV ABI）：
 *   进入时 %esp → argc，其后为 argv[]、NULL、envp[]、NULL、auxv…
 *   清 %ebp，取出 argc/argv/envp，按 cdecl 调用 main，再 exit。
 */
.code32
.section .text
.global _start
_start:
	# 准备栈上的环境
	xorl %ebp, %ebp			/* 最深栈帧标记（ABI） */
	popl %esi			/* argc；%esp → argv[0] */
	movl %esp, %ecx			/* argv */
	/* envp = argv + argc + 1（跳过 argv 指针与结尾 NULL） */
	leal 4(%esp,%esi,4), %eax

	andl $0xfffffff0, %esp		/* 调用前 16 字节对齐 */
	subl $4, %esp			/* 为 3 个参数 + 返回地址对齐留空 */
	pushl %eax			/* envp */
	pushl %ecx			/* argv */
	pushl %esi			/* argc */
	call main
	pushl %eax
	call exit
1:	jmp 1b
