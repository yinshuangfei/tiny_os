.code16			# 告诉编译器接下来的代码是 16 位模式（BIOS 启动时的 CPU 状态）

/*
 * MBR：把 kernel.bin（setup + body）读到 0x7e00，再跳转 setup。
 *
 * 软盘 INT 13h 走 ISA DMA，单次传输不得跨越物理 64KB 边界；
 * 且 CHS 读不得跨越磁道（QEMU 常容忍 multitrack，VMware 会 VERIFY 失败）。
 * 因此每次分块取三者之小：剩余扇区 / 本 DMA 页剩余 / 本磁道剩余。
 * 地址连续，读完后仍是一份完整镜像。
 */

.set STAGE2_TMP, 0x7e00
.set STAGE2_SECTORS, KERNEL_SECTORS
.set SPT, 18				/* 1.44MB 软盘扇区/磁道 */
.set HEADS, 2
.set LBA0, 1				/* 跳过本扇区（MBR），从第 2 扇区起读 */

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

	# 打开 A20：后续块可能落到 0x10000 以上，避免地址回绕
	inb $0x92, %al
	orb $0x02, %al
	outb %al, $0x92

	movb boot_drive, %dl
	xorb %ah, %ah			# AH=0 复位磁盘
	int $0x13

	xorw %si, %si			# si = 已读扇区数

	# 从第 2 扇区起读取 STAGE2_SECTORS 个扇区到 0x7e00
read_loop:
	cmpw $STAGE2_SECTORS, %si
	jae load_done

	# di = 剩余扇区
	movw $STAGE2_SECTORS, %di
	subw %si, %di

	# ES:BX = STAGE2_TMP + si*512
	# 起点扇区对齐 → BX 恒为 0，ES = (STAGE2_TMP>>4) + si*32
	movw %si, %ax
	shlw $5, %ax			# 将 si 左移 5 位，相当于乘以 32
	addw $(STAGE2_TMP >> 4), %ax
	movw %ax, %es
	xorw %bx, %bx

	# cx = min(剩余, 本 DMA 页可容纳扇区)；phys_lo==0 → 整页 128
	shlw $4, %ax			# ax = phys & 0xffff
	movw $128, %cx
	testw %ax, %ax
	jz 1f
	negw %ax
	shrw $9, %ax
	movw %ax, %cx
1:
	cmpw %di, %cx
	jbe 2f
	movw %di, %cx			# 不超过剩余扇区
2:
	# LBA = LBA0 + si → CHS（1.44MB：18 扇区/道，2 磁头）
	#   sector = (lba % SPT) + 1
	#   temp   = lba / SPT
	#   head   = temp % HEADS
	#   cyl    = temp / HEADS
	# 同时 cx = min(cx, 本道剩余) = min(cx, SPT - lba%SPT)
	movw %si, %ax
	addw $LBA0, %ax			# 当前逻辑块地址（跳过 MBR）
	xorw %dx, %dx
	movw $SPT, %bx
	# 被除数默认 32 位，存放在 DX:AX 寄存器对中，除数 16 位
	divw %bx			# ax=temp, dx=lba%SPT
	movw %dx, %di
	incw %di			# di = sector (1..SPT)
	movw $SPT, %bx
	subw %dx, %bx			# bx = 本道剩余扇区
	cmpw %bx, %cx
	jbe 3f
	movw %bx, %cx
3:
	movb %cl, nsect			# 本次读取扇区数（随后 CH 编码会占用 cx）

	xorw %dx, %dx
	movw $HEADS, %bx
	divw %bx			# ax=cylinder, dx=head

	# 填 INT 13h AH=02 的 CHS 寄存器：
	#   DH = head
	#   CH = cylinder[7:0]
	#   CL = sector[5:0] | (cylinder[9:8] << 6)
	movb %dl, %dh
	movb %al, %ch
	movb %ah, %cl
	shlb $6, %cl
	movw %di, %ax
	andb $0x3f, %al
	orb %al, %cl

	# ES:BX 已指向本块缓冲区；BX 在 CHS 计算中被改过，需重新清零
	xorw %bx, %bx
	movw $0x0200, %ax		# AH=02 读盘，AL=扇区数
	movb nsect, %al
	movb boot_drive, %dl
	int $0x13
	jc disk_error

	# 累计已读扇区，继续下一块（若有）
	movb nsect, %al
	xorb %ah, %ah
	addw %ax, %si
	jmp read_loop

load_done:
	xorw %ax, %ax
	movw %ax, %es
	# loader 仍在实模式运行，需用实模式远跳转到 0x7e00
	ljmp $0, $STAGE2_TMP

disk_error:
	hlt
	jmp disk_error

boot_drive:
	.byte 0				# 保存启动盘号的 1 字节变量，初始为 0

nsect:
	.byte 0				# 当前分块要读的扇区数

# boot_params.sector_count 由 Makefile 通过 -defsym KERNEL_SECTORS= 注入
.org 0x1F0
boot_params:
	.word KERNEL_SECTORS

.org 510				# 强制当前位置到偏移 510 字节处
	.word 0xaa55			# MBR 结束标志，0xaa55
