/*
 * AP real-mode trampoline（对齐 Linux：链进内核，BSP memcpy 到低地址）。
 * SIPI 向量页 = TRAMP；立即数按 TRAMP 编码，与链接 VMA 无关。
 *
 * BSP 将 [trampoline, etrampoline) 拷到 TRAMP，写启动参数后发 INIT-SIPI-SIPI。
 *
 * 启动参数（物理地址，紧挨页下方）：
 *   TRAMP - 4  = AP 栈顶
 *   TRAMP - 8  = start_secondary
 *   TRAMP - 12 = kernel_pgdir
 */
.set TRAMP, 0x8000

.globl trampoline, etrampoline
.section .rodata
.code16
.balign 4
trampoline:
	cli
	xorw	%ax, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	lgdtl	TRAMP + (gdtdesc - trampoline)
	movl	%cr0, %eax
	orb	$1, %al			/* PE, 置位保护模式位 */
	movl	%eax, %cr0
	ljmpl	$0x08, $TRAMP + (startup32 - trampoline) /* 刷新流水线 / 切 CS */

.code32
startup32:
	movw	$0x10, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	movw	%ax, %fs
	movw	%ax, %gs

	movl	TRAMP - 12, %eax	/* kernel_pgdir */
	movl	%eax, %cr3
	movl	%cr0, %eax
	orl	$0x80000001, %eax	/* PG|PE, 置位分页模式位和保护模式位 */
	movl	%eax, %cr0

	movl	TRAMP - 4, %esp
	call	*(TRAMP - 8)		/* start_secondary */

1:	hlt
	jmp	1b

.balign 4
gdt:
	.quad	0
	.quad	0x00cf9a000000ffff	/* code */
	.quad	0x00cf92000000ffff	/* data */
gdtdesc:
	.word	23			/* limit */
	.long	TRAMP + (gdt - trampoline)
etrampoline:
