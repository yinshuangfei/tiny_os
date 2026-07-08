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
.extern kernel_pgdir
.extern current_user_pgdir

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
	/*
	 * 同时处理从 ring3（用户态）和从 ring0（内核态）两种进入方式。

	 * ring3 进入时栈上已有 eip/cs/eflags/esp/ss；须先切回内核页表，
	 * 否则 ISR 自身无法取指。ring0 的 int 0x80 仍走 TRAP_FRAME_ENTER。
	 */
	movl 4(%esp), %ecx
	testl $3, %ecx		/* ecx & 3，只保留最低 2 位，0:kernel, 3:user */
	jz syscall_from_kernel

	/* ring3 路径 */
	pushl $0		/* err 占位，与 struct trapframe 对齐 */
	pushl %ds		/* 保存用户 ds */
	pushal			/* 保存 eax~edi（此时 eax 仍是用户的 42） */

	/* 切换到内核页表
	 * ISR 代码在内核 VA，必须先切回 kernel_pgdir 才能继续执行
	 */
	movl kernel_pgdir, %eax
	movl %eax, %cr3

	movw $0x10, %ax		# SEG_KDATA
	movw %ax, %ds
	movl %esp, %eax		# eax = trapframe 指针
	pushl %eax		# 保存 trapframe 地址
	call syscall_handler
	addl $4, %esp		# 弹出 C 函数参数
	popal			# 恢复通用寄存器（esp 回到 ds 位置）
	popl %ds		# 恢复用户 ds
	addl $4, %esp		/* 弹出 err 占位，栈顶对齐 iret 硬件帧 */

	/* 恢复用户页表 */
	movl current_user_pgdir, %eax
	movl %eax, %cr3
	iret			# 弹出 eip/cs/eflags/esp/ss → 回到 ring3

syscall_from_kernel:
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
	/* 若从 ring3 打断，须先切内核页表以便 ISR 可执行 */
	movl 4(%esp), %ecx
	testl $3, %ecx
	jz timer_from_kernel
	movl kernel_pgdir, %eax
	movl %eax, %cr3
timer_from_kernel:
	IRQSTACK_SWITCH
	call timer_handler
	IRQSTACK_RESTORE
	/* 仅 ring0 被打断时检查抢占；ring3 帧在栈上，不能在此 yield */
	movl 4(%esp), %ecx
	testl $3, %ecx
	jnz timer_to_user
	call preempt_check
timer_to_user:
	/* 返回 ring3 前恢复用户页表 */
	movl 4(%esp), %ecx
	testl $3, %ecx
	jz timer_iret
	movl current_user_pgdir, %eax
	movl %eax, %cr3
timer_iret:
	iret
