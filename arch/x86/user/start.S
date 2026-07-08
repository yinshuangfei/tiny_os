/*
 * 用户 C 程序入口：call main，再以 main 返回值调用 exit。
 */
.code32
.section .text
.global _start
_start:
	call main
	pushl %eax
	call exit
1:	jmp 1b
