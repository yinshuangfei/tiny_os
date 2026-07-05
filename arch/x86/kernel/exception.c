
/**
用户态陷入内核态的主要方式:
- 系统调用（System Call）：这是应用程序主动请求操作系统服务的唯一合法途径。
- 异常（Exception）：当 CPU 在用户态执行指令时遇到无法处理的情况（如缺页异常
     Page Fault、除零错误、非法指令等），会由 CPU 自动触发，被动切换到内核态交由操作
     系统的异常处理程序处理。
     - 故障（Fault）：可恢复，重试指令
         - 缺页异常（Page Fault）
         - 除零错误（Divide by Zero）
         - 一般保护异常（General Protection）
         - 非法指令（Illegal Instruction）
     - 陷阱（Trap）：已执行，推进指令
         - 断点异常（int 3）
         - 溢出异常（into）
         - 传统的系统调用（int 0x80 本质上也是一种陷阱）
     - 终止（Abort）：不可恢复，直接崩溃
         - 机器检查异常（Machine Check）
         - 总线错误（Bus Error）
- 外部中断（Interrupt）：硬件设备（如键盘、网卡、定时器）发出中断信号，CPU 暂停当前
     用户态任务，切换到内核态执行相应的中断服务例程。
*/
#include "defs.h"
#include "interrupt.h"
#include "x86.h"

void divide_error_handler(struct trapframe *tf)
{
	printf("divide error at eip=0x%x\n", tf->eip);
	panic("divide error");
}

void device_not_available_handler(struct trapframe *tf)
{
	uint32 cr0;

	printf("device not available (#NM) at eip=0x%x\n", tf->eip);
	/* lazy FPU：清 TS 后返回，让当前上下文继续执行 SSE/FP 指令 */
	cr0 = r_cr0();
	cr0 &= ~CR0_TS;
	w_cr0(cr0);
}

void invalid_opcode_handler(struct trapframe *tf)
{
	printf("invalid opcode (#UD) at eip=0x%x\n", tf->eip);
	panic("invalid opcode");
}

void general_protection_handler(struct trapframe *tf)
{
	printf("general protection (#GP) err=0x%x eip=0x%x\n", tf->err, tf->eip);
	panic("general protection");
}

void simd_fp_handler(struct trapframe *tf)
{
	printf("simd fp exception (#XM) at eip=0x%x\n", tf->eip);
	panic("simd fp exception");
}

void syscall_handler(struct trapframe *tf)
{
	printf("syscall: eax=0x%x\n", tf->eax);
}
