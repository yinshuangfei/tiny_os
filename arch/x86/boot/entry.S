.code32
.section .text

.globl _start
.type _start, @function

.extern main
.extern edata
.extern end
.extern kernel_stacks

.set KSTACKSIZE, 4096

_start:
	cli
	cld

	/* BSP 调度栈 = kernel_stacks[0] */
	movl $(kernel_stacks + KSTACKSIZE), %esp

	/* clear .bss */
	movl $edata, %edi
	movl $end, %ecx
	subl %edi, %ecx
	jz 1f			# 跳转到下面的 1: 处, f：代表 forward
	xorl %eax, %eax
	rep stosb
1:
	call main

hang:
	hlt
	jmp hang

.ifdef KERNEL
.section .note.Xen, "a", @note
	.align 4
	.long 4
	.long 8
	.long 18
	.ascii "Xen"
	.byte 0
	.align 4
	.long _start
	.long 0
.endif
