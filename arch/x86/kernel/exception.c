
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
#include "printk.h"
#include "trap.h"
#include "x86.h"
#include "proc.h"
#include "mm/memlayout.h"
#include "mm/mmu.h"
#include "gdt.h"
#include "ipc/signal.h"

/* #PF error code */
#define PF_P	0x1	/* 1=protection，0=not-present */
#define PF_W	0x2	/* 1=write */
#define PF_U	0x4	/* 1=user */

void divide_error_handler(struct trapframe *tf)
{
	printk(KERN_INFO "divide error at eip=0x%x\n", tf->eip);
	exit_signal(SIGSEGV);
	panic("divide error");
}

void device_not_available_handler(struct trapframe *tf)
{
	/* handle device not available (#NM) */
	/*
	 * 延迟切换:
	 * TS 是“延迟切换”的钩子；真正切换发生在 #NM 里, 也就是这里。
	 */
	fpu_nm(tf);
}

void invalid_opcode_handler(struct trapframe *tf)
{
	printk(KERN_INFO "invalid opcode (#UD) at eip=0x%x\n", tf->eip);
	exit_signal(SIGSEGV);
	panic("invalid opcode");
}

/*
 * #GP 异常是不可屏蔽/不可禁用的（#GP cannot be disabled），这是硬件级的强制保护机制
 */
void general_protection_handler(struct trapframe *tf)
{
	printk(KERN_INFO "general protection (#GP) err=0x%x eip=0x%x\n", tf->err, tf->eip);
	exit_signal(SIGSEGV);
	panic("general protection");
}

/*
 * 缺页：先按需填零（not-present），再处理 COW 写保护；失败则杀用户进程。
 * 内核态缺页仍 panic（教学内核无 fixup）。
 */
void page_fault_handler(struct trapframe *tf)
{
	uint32 fault_va;	/* 缺页的虚拟地址 */
	struct proc *p;
	int user;
	int write;

	__asm__ volatile ("movl %%cr2, %0" : "=r"(fault_va));

	user = tf && ((tf->cs & 3) == DPL_USER);
	p = myproc();
	write = tf && (tf->err & PF_W);

	/*
	 * 用户 not-present（典型 err=0x4/0x6）：堆或匿名 mmap 按需填零。
	 */
	if (user && p && proc_pagetable(p) && (tf->err & PF_U) &&
	    !(tf->err & PF_P)) {
		if (uvm_demand_fault(p, fault_va, write) == 0)
			return;
	}

	/*
	 * 用户写已存在页（典型 err=0x7）：若为 COW，复制后返回重试指令。
	 */
	if (user && p && proc_pagetable(p) && (tf->err & PF_U) && write &&
	    (tf->err & PF_P)) {
		if (fault_va >= USERBASE && fault_va < proc_task_size(p) &&
		    uvm_cow_fault(proc_pagetable(p), fault_va) == 0)
			return;
	}

	printk(KERN_ERR "page fault at va=%p err=0x%x eip=0x%x pid=%d\n",
	       (void *)fault_va, tf ? tf->err : 0,
	       tf ? tf->eip : 0, p ? p->pid : -1);

	if (user && p && proc_pagetable(p)) {
		/* 用户非法访问：以 SIGSEGV 退出（WIFSIGNALED） */
		exit_signal(SIGSEGV);
	}
	panic("page fault");
}

void simd_fp_handler(struct trapframe *tf)
{
	/* handle simd fp exception (#XM)*/
	struct proc *p = myproc();
	int user;

	user = tf && ((tf->cs & 3) == DPL_USER);
	printk(KERN_ERR "simd fp exception (#XM) at eip=0x%x pid=%d\n",
	       tf ? tf->eip : 0, p ? p->pid : -1);
	if (user && p && proc_pagetable(p)) {
		exit_signal(SIGFPE);
	}
	panic("simd fp exception");
}

void syscall_handler(struct trapframe *tf)
{
	syscall(tf);
	/* int 0x80 硬件已将 eip 设为下一条指令，勿再 += 2 */
	signal_notify(tf);
}
