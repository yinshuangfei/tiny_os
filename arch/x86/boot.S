.code16			# 告诉编译器接下来的代码是 16 位模式（BIOS 启动时的 CPU 状态）

.set STAGE2_TMP, 0x7e00
.set STAGE2_SECTORS, 2
.set CODE_SEL, 0x08

.section .text
.globl _boot
.type _boot, @function	# 告诉调试器 _boot 是函数

_boot:
	# MBR 只做最小引导工作：把二级代码先读到低地址临时缓冲区
	cli				# 关中断（Clear Interrupt）
	xorw %ax, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss			# (Stack Segment) - 栈段寄存器
	# SP (Stack Pointer) - 栈指针寄存器
	movw $0x7c00, %sp		# 栈指针设置，int 0x13 会破坏寄存器，所以需要保存
	# int 在实模式下会把 FLAGS、CS、IP 压栈（共 6 字节）。

	# 在 BIOS 传统启动（Legacy Boot）下，是的——BIOS 把控制权交给引导扇区
	#（跳转到 0x7C00）时，启动盘号约定放在 DL 里
	movb %dl, boot_drive		# 保存 BIOS 传入的启动盘号（boot_drive 为内存地址，加 $表示值）

	# 这个最小 demo 的二级代码只有一个扇区，因此直接读取 sector 2。
	movw $STAGE2_TMP, %bx		# BX=0x7e00 (二级代码临时缓冲区)。
	movw $(0x0200 | STAGE2_SECTORS), %ax	# AH=0x02 (读扇区)，AL=扇区数
	movw $0x0002, %cx		# CH=0x00 (柱面号)，CL=0x02 (扇区号)。第 1 扇区是 MBR 自己
	movb boot_drive, %dl
	int $0x13			# 磁盘 I/O 服务, 0x13 中断返回至下一条指令，CF=0 表示成功，CF=1 表示失败。
	# Jump if Carry
	jc disk_error

	# loader 仍在实模式运行，需用实模式远跳转到 0x7e00
	ljmp $0, $STAGE2_TMP

disk_error:
	hlt
	jmp disk_error

boot_drive:
	.byte 0				# 保存启动盘号的 1 字节变量，初始为 0

.org 510				# 强制当前位置到偏移 510 字节处
	.word 0xaa55			# MBR 结束标志，0xaa55
