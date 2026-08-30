.code32
.section .text.boot, "ax"

.globl _start
.type _start, @function

.extern main
.extern edata
.extern end
.extern kernel_stacks

.set KSTACKSIZE, 4096
.set KERNBASE, 0xC0000000
.set PDE_P, 0x001
.set PDE_W, 0x002
.set PDE_PS, 0x080

_start:
	cli
	cld

	/* BSP 临时栈：分页开启前先用内核栈的物理别名 */
	movl $(kernel_stacks + KSTACKSIZE - KERNBASE), %esp

	/* clear .bss（高半区符号需先落回物理别名） */
	movl $(edata - KERNBASE), %edi
	movl $(end - KERNBASE), %ecx
	subl %edi, %ecx
	jz 1f			# 跳转到下面的 1: 处, f：代表 forward
	xorl %eax, %eax
	rep stosb
1:
	movl $entry_pgdir, %eax
	movl %eax, %cr3
	movl %cr4, %eax
	orl $0x10, %eax		# CR4.PSE
	movl %eax, %cr4
	movl %cr0, %eax
	orl $0x80000000, %eax
	movl %eax, %cr0
	jmp 2f
2:
	movl $(kernel_stacks + KSTACKSIZE), %esp
	call main

hang:
	hlt
	jmp hang

.section .data.boot, "aw"
	.align 4096
entry_pgdir:
	.set i, 0
	.long (0 << 22) | PDE_P | PDE_W | PDE_PS
	.fill 767, 4, 0
	.rept 256
	.long (i << 22) | PDE_P | PDE_W | PDE_PS
	.set i, i + 1
	.endr

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
