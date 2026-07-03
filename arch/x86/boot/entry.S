.code32

.macro SERIAL_PUTC
serial_putc:
	movb 4(%esp), %al
	movb %al, %ah

wait_tx_ready:
	movw $0x3fd, %dx
	inb %dx, %al
	testb $0x20, %al
	je wait_tx_ready
	movb %ah, %al
	movw $0x3f8, %dx
	outb %al, %dx
	ret
.endm

.section .text
.globl serial_putc
SERIAL_PUTC

.globl _start
.type _start, @function

.extern main
.extern edata
.extern end

_start:
	cld
	movl $0x80000, %esp

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
