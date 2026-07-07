.code32

.set KSTACKSIZE, 4096

.globl interrupt_stack
.globl isr_divide_error
.globl isr_device_not_available
.globl isr_invalid_opcode
.globl isr_general_protection
.globl isr_page_fault
.globl isr_simd_fp
.globl isr_syscall
.globl isr_timer

.extern divide_error_handler
.extern device_not_available_handler
.extern invalid_opcode_handler
.extern general_protection_handler
.extern page_fault_handler
.extern simd_fp_handler
.extern syscall_handler
.extern timer_handler

/*
 * 切换到 interrupt_stack，栈顶保存原 ESP（指向 trapframe）。
 * C 处理在中断栈上执行；返回前用 IRQSTACK_RESTORE 回到被中断上下文。
 */
.macro IRQSTACK_SWITCH
	movl %esp, %eax
	movl $(interrupt_stack + KSTACKSIZE), %esp
	pushl %eax
.endm

.macro IRQSTACK_RESTORE
	popl %esp
.endm

/*
 * 统一 trapframe 布局（esp 指向 edi）：
 *   pushal → ds → err → eip → cs → eflags
 * 无硬件 error code 的向量在 TRAP_FRAME_ENTER 前先 pushl $0 占位。
 */
.macro TRAP_FRAME_ENTER
	pushl %ds
	pushal
	movw $0x10, %ax
	movw %ax, %ds
	movl %esp, %eax
	IRQSTACK_SWITCH
	pushl %eax
.endm

.macro TRAP_FRAME_LEAVE
	addl $4, %esp
	IRQSTACK_RESTORE
	popal
	popl %ds
	addl $4, %esp
	iret
.endm

isr_divide_error:
	pushl $0
	TRAP_FRAME_ENTER
	call divide_error_handler
	TRAP_FRAME_LEAVE

isr_invalid_opcode:
	pushl $0
	TRAP_FRAME_ENTER
	call invalid_opcode_handler
	TRAP_FRAME_LEAVE

isr_device_not_available:
	pushl $0
	TRAP_FRAME_ENTER
	call device_not_available_handler
	TRAP_FRAME_LEAVE

isr_general_protection:
	TRAP_FRAME_ENTER
	call general_protection_handler
	TRAP_FRAME_LEAVE

isr_simd_fp:
	pushl $0
	TRAP_FRAME_ENTER
	call simd_fp_handler
	TRAP_FRAME_LEAVE

isr_syscall:
	pushl $0
	TRAP_FRAME_ENTER
	call syscall_handler
	TRAP_FRAME_LEAVE

isr_page_fault:
	TRAP_FRAME_ENTER
	call page_fault_handler
	TRAP_FRAME_LEAVE

/*
 * 定时器 IRQ：切 interrupt_stack 调 C；返回被中断栈后再检查抢占并 iret。
 */
.extern preempt_check

isr_timer:
	IRQSTACK_SWITCH
	call timer_handler
	IRQSTACK_RESTORE
	call preempt_check
	iret
