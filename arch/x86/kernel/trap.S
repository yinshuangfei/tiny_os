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
.extern timer_trap_user
.extern kernel_pgdir
.extern current_user_pgdir

/*
 * 切换到 interrupt_stack，栈顶保存原 ESP（指向 trapframe）。
 * C 处理在中断栈上执行；返回前用 IRQSTACK_RESTORE 回到被中断上下文。
 */
.macro IRQSTACK_SWITCH
	movl %esp, %eax		# eax = 指向 trapframe (edi)
	movl $(interrupt_stack + KSTACKSIZE), %esp	# 切换到中断栈
	pushl %eax		# 中断栈栈顶，保存「原栈顶」，供 RESTORE
.endm

.macro IRQSTACK_RESTORE
	popl %esp		# 中断栈，pop → ESP 回到原始内核栈
.endm

/*
 * 统一 trapframe 布局（esp 指向 edi）：
 * pushal → ds → err → eip → cs → eflags
 * 无硬件 error code 的向量在 TRAP_FRAME_ENTER 前先 pushl $0 占位。
 */
.macro TRAP_FRAME_ENTER
	pushl %ds
	pushal
	movw $0x10, %ax		# SEG_KDATA, DPL=0
	movw %ax, %ds
	movl %esp, %eax
	IRQSTACK_SWITCH
	pushl %eax		# 中断栈，压入 trapframe，作 call 的 C 参数

	/* 后续的 C 代码在中断栈上跑，不占用被中断代码的内核栈深度 */
.endm

.macro TRAP_FRAME_LEAVE
	addl $4, %esp		# 弹出中断栈 C 函数参数，栈清理
	IRQSTACK_RESTORE	# pop → ESP 回到原栈 trapframe
	popal			# 恢复通用寄存器（esp 回到 ds 位置）
	popl %ds
	addl $4, %esp		# 弹 err
	iret			# 只弹 eip/cs/eflags（ring0→ring0）
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
	 * (1) ring3 进入时栈上已有 eip/cs/eflags/esp/ss；
	 * 须先切回内核页表，否则 ISR 自身无法取指。
	 *
	 * (2) ring0 进入时栈上已有 eip/cs/eflags/。
	 * ring0 的 int 0x80 仍走 TRAP_FRAME_ENTER。
	 *   esp+40  eip        ← 指令指针
	 *   esp+44  cs         ← 代码段选择子
	 *   esp+48  eflags     ← 标志寄存器
	 *   esp+52  ???        ← struct 里有字段，但 ring0 无对应硬件压栈
	 *   esp+56  ???        ← 同上

	 * 用 test 直接读栈上 CS，勿占用 eax~ebp（系统调用参数寄存器）。
	 */
	testl $3, 4(%esp)
	jz syscall_from_kernel

	/* ring3 路径, 补全 trapframe 布局 */
	pushl $0		/* err 占位，与 struct trapframe 对齐 */
	pushl %ds		/* 保存用户 ds */
	pushal			/* 保存 eax~edi（含系统调用号与参数） */

	/*
	 * 切换到内核页表
	 * ISR 代码在内核 VA，必须先切回 kernel_pgdir 才能继续执行
	 */
	movl kernel_pgdir, %eax
	movl %eax, %cr3

	/* 切换到内核数据段 */
	movw $0x10, %ax		# SEG_KDATA
	movw %ax, %ds

	/* trapframe 内容和指示位置已准备妥当，直接传给 C 函数 */
	movl %esp, %eax		# eax = trapframe 指针
	pushl %eax		# 保存 trapframe 地址，使用栈传递参数

	call syscall_handler

	/* 栈清理 */
	addl $4, %esp		# 弹出 C 函数参数
	popal			# 恢复通用寄存器（esp 回到 ds 位置）
	popl %ds		# 恢复用户 ds
	addl $4, %esp		/* 弹出 err 占位，栈顶对齐 iret 硬件帧 */

	/* 恢复用户页表 */
	movl current_user_pgdir, %eax
	movl %eax, %cr3
	iret			# 弹出 eip/cs/eflags/esp/ss → 回到 ring3

syscall_from_kernel:
	/* 从 ring0 触发 int、异常、IRQ 时，只在当前栈上压返回信息。 */
	pushl $0		/* err 占位 */
	TRAP_FRAME_ENTER
	call syscall_handler
	TRAP_FRAME_LEAVE

isr_page_fault:
	TRAP_FRAME_ENTER
	call page_fault_handler
	TRAP_FRAME_LEAVE

/*
 * 定时器 IRQ：ring0 走 interrupt_stack；ring3 保存 trapframe 后可 yield 抢占。
 */
.extern preempt_check

isr_timer:
	/* 若从 ring3 打断，须先切内核页表以便 ISR 可执行 */
	testl $3, 4(%esp)
	jz timer_from_kernel

	/* ring3：与 isr_syscall 相同的 trapframe 布局 */
	pushl $0		/* err 占位 */
	pushl %ds
	pushal

	movl kernel_pgdir, %eax
	movl %eax, %cr3
	movw $0x10, %ax
	movw %ax, %ds

	movl %esp, %eax		/* trapframe 指针 */
	pushl %eax
	call timer_trap_user
	addl $4, %esp

	call preempt_check

	movl current_user_pgdir, %eax
	movl %eax, %cr3
	popal
	popl %ds
	addl $4, %esp		/* err 占位 */
	iret

timer_from_kernel:
	IRQSTACK_SWITCH
	call timer_handler
	IRQSTACK_RESTORE
	call preempt_check
	iret
