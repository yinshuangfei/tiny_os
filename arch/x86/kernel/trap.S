.code32

.globl isr_page_fault
.globl isr_timer
.globl isr_divide_error
.globl isr_invalid_opcode
.globl isr_syscall

isr_page_fault:
	pushl %ds
	pushal
	movw $0x10, %ax
	movw %ax, %ds
	pushl %esp
	call page_fault_handler
	addl $4, %esp
	popal
	popl %ds
	addl $4, %esp		# pop CPU-pushed error code
	iret

isr_timer:
	pusha			# 保存全部 8 个通用寄存器
	call timer_handler
	popa
	movb $0x20, %al		# 0x20 是主 PIC 的 EOI 信号, 0x20 = End of Interrupt
	outb %al, $0x20		# 写到主 PIC 命令口（0x20），通知 8259「这条 IRQ 已处理完」
	iret

isr_divide_error:
	pushl $0		# 除零异常无 CPU 压入的 error code
	pushl %ds
	pushal
	movw $0x10, %ax
	movw %ax, %ds
	pushl %esp
	call divide_error_handler
	addl $4, %esp
	popal
	popl %ds
	addl $4, %esp		# pop 占位 error code
	iret

isr_invalid_opcode:
	pushl $0		# #UD 无 error code
	pushl %ds
	pushal
	movw $0x10, %ax
	movw %ax, %ds
	pushl %esp
	call invalid_opcode_handler
	addl $4, %esp
	popal
	popl %ds
	addl $4, %esp
	iret

isr_syscall:
	pushl $0		# int 0x80 不压 error code，占位以匹配 struct trapframe
	pushl %ds
	pushal			# 保存寄存器
	movw $0x10, %ax
	movw %ax, %ds
	pushl %esp
	call syscall_handler
	addl $4, %esp
	popal
	popl %ds
	iret
