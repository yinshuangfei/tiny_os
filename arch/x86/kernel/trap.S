.code32

/* --------------------------------------------------------------------------
 * 符号与常量
 * -------------------------------------------------------------------------- */

.set KSTACKSIZE, 4096
.set SEG_KDATA,  0x10

/* trapframe 字段偏移（esp 指向 edi，与 trap.h 一致） */
.set TF_OESP, 12
.set TF_EAX,  28

/* 陷入时硬件帧：testl $3, HW_CS(%esp) 判断 ring3（CS.RPL） */
.set HW_CS, 4

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
.extern preempt_check
.extern kernel_pgdir
.extern current_user_pgdir

/* --------------------------------------------------------------------------
 * 中断栈切换
 * -------------------------------------------------------------------------- */

/*
 * 切换到 interrupt_stack，栈顶保存原 ESP（指向 trapframe）。
 * C 处理在中断栈上执行；返回前用 IRQSTACK_RESTORE 回到被中断上下文。
 */
.macro IRQSTACK_SWITCH
	movl %esp, %eax		# eax = 指向 trapframe (edi)
	movl $(interrupt_stack + KSTACKSIZE), %esp	# 切到 interrupt_stack 栈顶
	pushl %eax		# 中断栈栈顶，保存「原栈顶」，供 RESTORE
.endm

.macro IRQSTACK_RESTORE
	popl %esp		# pop → ESP 回到原始内核栈
.endm

/* --------------------------------------------------------------------------
 * ring0 trap：补全 trapframe，在中断栈上调用 C handler
 * -------------------------------------------------------------------------- */

/*
 * 统一 trapframe 布局（esp 指向 edi）：
 * pushal → ds → err → eip → cs → eflags
 * 无硬件 error code 的向量在 TRAP_FRAME_ENTER 前先 pushl $0 占位。
 */
.macro TRAP_FRAME_ENTER
	pushl %ds
	pushal
	movw $SEG_KDATA, %ax	# SEG_KDATA, DPL=0
	movw %ax, %ds
	movl %esp, %eax
	IRQSTACK_SWITCH
	pushl %eax		# 中断栈，压入 trapframe，作 call 的 C 参数

	/* 后续的 C 代码在中断栈上跑，不占用被中断代码的内核栈深度 */
.endm

.macro TRAP_FRAME_LEAVE
	addl $4, %esp		# 弹出中断栈 C 函数参数
	IRQSTACK_RESTORE	# pop → ESP 回到原栈 trapframe
	TRAP_RESTORE_REGS
	iret			# 只弹 eip/cs/eflags（ring0→ring0）
.endm

/* 无硬件 error code 的异常向量 */
.macro ISR_EXCEPTION handler
	pushl $0
	TRAP_FRAME_ENTER
	call \handler
	TRAP_FRAME_LEAVE
.endm

/* 硬件已压入 error code 的异常向量（如 #GP、#PF） */
.macro ISR_EXCEPTION_ERR handler
	TRAP_FRAME_ENTER
	call \handler
	TRAP_FRAME_LEAVE
.endm

/* --------------------------------------------------------------------------
 * ring3 trap：补全 trapframe，切内核环境；C handler 在 kstack 上运行
 * -------------------------------------------------------------------------- */

/* ISR 代码在内核 VA，须先切回 kernel_pgdir 才能继续取指/访存 */
.macro SWITCH_TO_KERNEL
	movl kernel_pgdir, %eax
	movl %eax, %cr3
	movw $SEG_KDATA, %ax
	movw %ax, %ds
.endm

/* ring3 陷入：补全与 struct trapframe 对齐的软件压栈部分 */
.macro TRAP_RING3_ENTER
	pushl $0		/* err 占位，与 struct trapframe 对齐 */
	pushl %ds		/* 保存用户 ds */
	pushal			/* 保存 eax~edi（含系统调用号与参数） */
	SWITCH_TO_KERNEL
.endm

/*
 * 在进程 kstack 上调用 C handler（trapframe 下方压栈，栈向下生长不覆盖 trapframe）。
 * 内核运行中被中断时，由各 ISR 的 IRQSTACK_SWITCH 再切到 interrupt_stack。
 */
.macro TRAP_CALL_ON_KSTACK handler
	movl %esp, %eax		# eax = trapframe 指针
	pushl %eax		# C 参数
	call \handler
	addl $4, %esp		# 弹出 C 参数
.endm

/* --------------------------------------------------------------------------
 * 寄存器恢复与返回
 * -------------------------------------------------------------------------- */

/*
 * pushal 保存的 oesp 指向 trapframe 起点；popal 中途恢复 ESP 时会从该地址
 * 继续弹栈，导致 ebx/eax 等错位。将 oesp 修正为 pushal 块之后即可。
 */
.macro POPAL_FIX_OESP
	addl $16, TF_OESP(%esp)
.endm

/*
 * popal 之后 esp 指向 ds（+32）；syscall 返回值在 eax 槽（+28），即 -4(%esp)。
 */
.macro LOAD_SYSCALL_RET
	movl -4(%esp), %eax
.endm

/* 恢复用户页表：用 ecx 作暂存，勿破坏 eax 中的 syscall 返回值 */
.macro RESTORE_USER_PGDIR
	pushl %ecx
	movl current_user_pgdir, %ecx
	movl %ecx, %cr3
	popl %ecx
.endm

.macro TRAP_RESTORE_REGS
	POPAL_FIX_OESP
	popal			# 恢复通用寄存器（esp 回到 ds 位置）
	LOAD_SYSCALL_RET
	popl %ds
	addl $4, %esp		# 弹出 err 占位
.endm

/* ring3 完整返回：恢复寄存器、用户页表、iret（弹出 eip/cs/eflags/esp/ss） */
.macro TRAP_USER_RETURN
	TRAP_RESTORE_REGS
	RESTORE_USER_PGDIR
	iret
.endm

/* --------------------------------------------------------------------------
 * 异常向量
 * -------------------------------------------------------------------------- */

isr_divide_error:
	ISR_EXCEPTION divide_error_handler

isr_invalid_opcode:
	ISR_EXCEPTION invalid_opcode_handler

isr_device_not_available:
	ISR_EXCEPTION device_not_available_handler

isr_general_protection:
	ISR_EXCEPTION_ERR general_protection_handler

isr_simd_fp:
	ISR_EXCEPTION simd_fp_handler

isr_page_fault:
	ISR_EXCEPTION_ERR page_fault_handler

/* --------------------------------------------------------------------------
 * 系统调用（int 0x80）
 * -------------------------------------------------------------------------- */

isr_syscall:
	/*
	 * 同时处理从 ring3（用户态）和从 ring0（内核态）两种进入方式。
	 *
	 * (1) ring3 进入时栈上已有 eip/cs/eflags/esp/ss；
	 *     须先切回内核页表，否则 ISR 自身无法取指。
	 *
	 * (2) ring0 进入时栈上已有 eip/cs/eflags。
	 *     ring0 的 int 0x80 走 TRAP_FRAME_ENTER：
	 *       esp+40  eip
	 *       esp+44  cs
	 *       esp+48  eflags
	 *       esp+52  ???   ← struct 里有字段，但 ring0 无对应硬件压栈
	 *       esp+56  ???   ← 同上
	 *
	 * 用 test 直接读栈上 CS，勿占用 eax~ebp（系统调用参数寄存器）。
	 */
	testl $3, HW_CS(%esp)
	jz syscall_from_kernel

	/* ring3：trapframe 在 kstack 顶，C 处理在同栈 trapframe 下方 */
	TRAP_RING3_ENTER
	TRAP_CALL_ON_KSTACK syscall_handler
	TRAP_USER_RETURN

syscall_from_kernel:
	/* 从 ring0 触发 int 0x80：只在当前栈上压 err，再走统一 trap 入口 */
	pushl $0		/* err 占位 */
	TRAP_FRAME_ENTER
	call syscall_handler
	TRAP_FRAME_LEAVE

/* --------------------------------------------------------------------------
 * 定时器 IRQ
 * -------------------------------------------------------------------------- */

/*
 * 定时器 IRQ：ring0 走 interrupt_stack；ring3 保存 trapframe 后可 yield 抢占。
 */
isr_timer:
	/* 若从 ring3 打断，须先切内核页表以便 ISR 可执行 */
	testl $3, HW_CS(%esp)
	jz timer_from_kernel

	/* ring3：trapframe 在 kstack 顶，timer 处理在同栈 trapframe 下方 */
	TRAP_RING3_ENTER
	TRAP_CALL_ON_KSTACK timer_trap_user
	call preempt_check
	TRAP_USER_RETURN

timer_from_kernel:
	IRQSTACK_SWITCH
	call timer_handler
	IRQSTACK_RESTORE
	call preempt_check
	iret
