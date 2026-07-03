.code32

.globl timervec
.type timervec, @function

timervec:
	pusha			# 保存全部 8 个通用寄存器
	call timer_handler
	popa
	movb $0x20, %al		# 0x20 是主 PIC 的 EOI 信号, 0x20 = End of Interrupt
	outb %al, $0x20		# 写到 主 PIC 命令口（0x20），通知 8259「这条 IRQ 已处理完」
	iret
