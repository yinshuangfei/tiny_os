/**
 * 在 RISC-V 架构中，共有 4 种不同特权级别：
 *
 * - 机器模式（M-mode）:在 M-mode 下，程序可以不受任何限制地访问所有的物理内存、I/O 设备
 * 以及所有的控制状态寄存器（CSR）。它的代码被认为是 100% 可信的。
 *
 * - 监督者模式（S-mode）:
 * S-mode 是一种中等特权级别，它的权限比 M-mode 低，但比用户模式（U-mode）高。
 * 在 S-mode 下，操作系统不能随意访问所有的硬件资源。例如，它无法直接访问物理内存保护
 * （PMP）寄存器，也无法访问 M-mode 专属的 CSR（如 mtvec, mstatus 等）。
 *
 * - Hypervisor 模式（H-mode）：
 * 这是专门为虚拟化设计的模式，用于支持虚拟机管理程序（Hypervisor）运行，提供虚拟机的
 * 隔离与调度。
 *
 * - 用户模式（User Mode，简称 U-mode）：
 * 这是最低的特权级别，普通的应用程序通常在这个模式下运行。在此模式下，程序无法直接访问
 * 敏感的硬件资源（如内存管理单元、I/O 设备等），必须通过系统调用接口（如 ecall）与操作
 * 系统（S-mode）进行交互，以防止用户程序干扰系统安全
 */

#include "spinlock.h"
#include "defs.h"
#include "proc.h"
#include "riscv.h"
#include "memlayout.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void trapinit(void)
{
	initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void trapinithart(void)
{
	// 赋值 trap 向量地址（作用于内核态）
	w_stvec((uint64)kernelvec);
}

#define SCAUSE_INTERRUPT (1L << 63)
void pr_trap(char *tag)
{
	// printf("usertrap\n");
	uint64 scause = r_scause();

	// https://github.com/riscv/riscv-isa-manual/releases/download/riscv-isa-release-cc30642-2026-06-23/riscv-spec.html
	// 异步中断
	if (scause & SCAUSE_INTERRUPT) {
		printf("[%s] Trap: Interrupt, Code: ", tag);
		if ((scause & 0xff) == 1) {
			printf("Supervisor software interrupt");
		} else if ((scause & 0xff) == 5) {
			printf("Supervisor timer interrupt");
		} else if ((scause & 0xff) == 9) {
			printf("Supervisor external interrupt");
		} else if ((scause & 0xff) == 13) {
			printf("Counter-overflow interrupt");
		} else {
			printf("Unknown interrupt");
		}
		printf("\n");

	}
	// 同步异常(异常|系统调用)
	else {
		printf("[%s] Trap: Exception, Code: ", tag);
		switch (scause & 0xff) {
		case 0:
			printf("Instruction address misaligned");
			break;
		case 1:
			printf("Instruction access fault");
			break;
		case 2:
			printf("Illegal instruction");
			break;
		case 3:
			printf("Breakpoint");
			break;
		case 4:
			printf("Load address misaligned");
			break;
		case 5:
			printf("Load access fault");
			break;
		case 6:
			printf("Store/AMO address misaligned");
			break;
		case 7:
			printf("Store/AMO access fault");
			break;
		case 8:
			printf("Environment call from U-mode");
			break;
		case 9:
			printf("Environment call from S-mode");
			break;
		case 12:
			printf("Instruction page fault");
			break;
		case 13:
			printf("Load page fault");
			break;
		case 15:
			printf("Store/AMO page fault");
			break;
		case 18:
			printf("Software check");
			break;
		case 19:
			printf("Hardware error");
			break;
		case 24 ... 31:
			printf("Designated for custom use");
			break;
		case 48 ... 63:
			printf("Designated for custom use");
			break;
		default:
			printf("Unknown exception");
			break;
		}
		printf("\n");
	}

	// printf("%s, scause %p\n", tag, scause);
	// printf("%s, sepc=%p stval=%p\n", tag, r_sepc(), r_stval());
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
// 用户态发生 trap 后进入：此时使用内核态权限执行
// 让 CPU 从用户态切到内核态的是 trap 机制, 进入 uservec 后变为内核态. 这是纯硬件机制.
// 进入函数前，中断已经关闭
// 退出时，从 spie 中恢复中断
void usertrap(void)
{
	int which_dev = 0;

	// 检查 trap 发生前的特权级，需要为用户态
	// - SPP == 0 -> 上一层是用户态
	// - SPP != 0 -> 上一层是内核态
	if ((r_sstatus() & SSTATUS_SPP) != 0)
		panic("usertrap: not from user mode");

	// pr_trap("User-into");

	// send interrupts and exceptions to kerneltrap(),
	// since we're now in the kernel.
	// 赋值 trap 向量地址（作用于内核态）
	/**
	 * 如果不改这个值，会有两个问题：
	 * - 内核态下的 trap 还可能错误地进 uservec;
	 * - uservec 的逻辑是按“从用户态进入内核”设计的，它会假设一些用户态上下文存在，
	 * 比如 sscratch、trapframe、用户页表布局等；这对内核态 trap 不一定成立;
	 */
	w_stvec((uint64)kernelvec);

	struct proc *p = myproc();

	// save user program counter.
	// CPU 发生异常或执行 ecall 陷入内核时，硬件会自动把当前的 PC（程序计数器）保存
	// 到 sepc 寄存器中。执行 sret 时，硬件直接将 sepc 的值赋给 PC。
	// 这里先保存 sepc。
	p->trapframe->epc = r_sepc();

	if (r_scause() == 8) {
		// system call

		if (p->killed)
			exit(-1);

		// sepc points to the ecall instruction,
		// but we want to return to the next instruction.
		// 让用户态在返回时跳过当前这条 ecall 指令，继续执行下一条指令
		// 如果不把 epc 往后加 4，usertrapret() 返回用户态后，CPU 会再次执行
		// 同一条 ecall, 结果就是不断重复陷入 trap，形成死循环.
		// +4 是因为：RISC-V 的 ecall 指令长度是 4 字节
		p->trapframe->epc += 4;

		// an interrupt will change sstatus &c registers,
		// so don't enable until done with those registers.
		// 处理 syscall 的时候，内核还能响应中断
		intr_on();

		syscall();
	}
	// 刚从用户态 trap 进入内核态时，进行一次中断处理，以免遗漏
	// 因为可能由于外部中断而 trap
	else if ((which_dev = devintr()) != 0) {
		// ok
	} else {
		printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
		printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
		p->killed = 1;
	}

	if (p->killed)
		exit(-1);

	// give up the CPU if this is a timer interrupt.
	// 如果是定时器中断，则让出 CPU
	if (which_dev == 2)
		yield();

	usertrapret();
}

//
// return to user space, 目前在内核态，最后一句话执行完后，内核态返回到用户态
//
void usertrapret(void)
{
	// printf("usertrapret\n");
	struct proc *p = myproc();

	// we're about to switch the destination of traps from
	// kerneltrap() to usertrap(), so turn off interrupts until
	// we're back in user space, where usertrap() is correct.
	intr_off();

	// send syscalls, interrupts, and exceptions to trampoline.S
	// 赋值中断向量地址 uservec.
	// 把 stvec 改成 uservec 的 trampoline 高地址版本,
	// 这样下次用户态执行 ecall、触发异常、或收到中断时，CPU 会先进 kernel/trampoline.S 的 uservec
	// uservec 再保存用户寄存器、切回内核页表、接着跳进 usertrap()。

	/**
	 * 赋值 trap 向量地址（作用于用户态）
	 * 用户态程序触发 trap:
	 * - 常见来源是 ecall、异常，或者用户态中断
	 * - 这时硬件会按 stvec 跳转，入口是 uservec, uservec 接着调用 usertrap()
	 */
	w_stvec(TRAMPOLINE + (uservec - trampoline));

	// set up trapframe values that uservec will need when
	// the process next re-enters the kernel.
	p->trapframe->kernel_satp = r_satp();         // kernel page table
	p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
	p->trapframe->kernel_trap = (uint64)usertrap; // 定义 uservec 中的跳转函数
	p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

	// set up the registers that trampoline.S's sret will use
	// to get to user space.

	// set S Previous Privilege mode to User.
	// 寄存器状态切换成用户态模式
	// 由硬件在 sret 时把 SPIE -> SIE，此时 CPU 已经回到用户态, 中断打开
	unsigned long x = r_sstatus();
	x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
	x |= SSTATUS_SPIE; // arrange for sret to restore SIE=1
	w_sstatus(x);

	// set S Exception Program Counter to the saved user pc.
	// CPU 发生异常或执行 ecall 陷入内核时，硬件会自动把当前的 PC（程序计数器）保存
	// 到 sepc 寄存器中。执行 sret 时，硬件直接将 sepc 的值赋给 PC
	// 复制 sepc 为 epc，为切换回用户程序做准备
	// 对第一个程序 initcode 特别重要
	w_sepc(p->trapframe->epc);

	// tell trampoline.S the user page table to switch to.
	uint64 satp = MAKE_SATP(p->pagetable);

	// jump to trampoline.S at the top of memory, which
	// switches to the user page table, restores user registers,
	// and switches to user mode with sret.
	// 调用 trampoline.S 中的 userret()，最后执行 sret
	uint64 fn = TRAMPOLINE + (userret - trampoline);

	// (void (*)(uint64, uint64))fn 是把这个地址强制转换成“一个接收两个 uint64
	// 参数、返回 void 的函数指针”。
	// 等价执行 userret(TRAPFRAME, satp);

	/** 为什么不直接写成 userret(TRAPFRAME, satp);?
	 * userret(TRAPFRAME, satp) 会按“内核链接地址”去跳，不保证在切换页表后还能
	 * 继续执行；而 TRAMPOLINE + (userret - trampoline) 保证跳到的是 trampoline
	 * 页在高地址上的运行时位置。
	*/
	// !!!调用 userret(), 内核态切换到用户态代码执行!!!
	((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
// 内核发生中断时，在 kernel/kernelvec.S 中会调用 kerneltrap()
// 这个函数里不能放 printf, 否则会循环触发 UART 中断
void kerneltrap()
{
	int which_dev = 0;
	uint64 sepc = r_sepc();
	uint64 sstatus = r_sstatus();
	uint64 scause = r_scause();

	if ((sstatus & SSTATUS_SPP) == 0)
		panic("kerneltrap: not from supervisor mode");

	if (intr_get() != 0)
		panic("kerneltrap: interrupts enabled");

	if ((which_dev = devintr()) == 0) {
		printf("scause %p\n", scause);
		printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
		panic("kerneltrap");
	}

	// give up the CPU if this is a timer interrupt.
	// 这里面的逻辑可能会改变 spec
	if (which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
		yield();

	// the yield() may have caused some traps to occur,
	// so restore trap registers for use by kernelvec.S's sepc instruction.
	w_sepc(sepc);
	w_sstatus(sstatus);
}

void clockintr()
{
	acquire(&tickslock);
	ticks++;
	wakeup(&ticks);
	release(&tickslock);
}


// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
// 中断处理函数
int devintr()
{
	uint64 scause = r_scause();

	if ((scause & 0x8000000000000000L) &&
	    (scause & 0xff) == 9) {
		// supervisor 模式(内核态)的外部中断
		// this is a supervisor external interrupt, via PLIC.

		// irq indicates which device interrupted.
		// 调用 plic_claim() 后，intr_on() 函数才不会卡住
		int irq = plic_claim();

		if (irq == UART0_IRQ) {
			uartintr();
			// printf("cpu:%d, uartintr interrupt irq=%d\n", cpuid(), irq);
		} else if (irq == VIRTIO0_IRQ) {
			// printf("virtio_disk_intr interrupt irq=%d\n", irq);
			virtio_disk_intr();
		} else if (irq) {
			printf("unexpected interrupt irq=%d\n", irq);
		}

		// the PLIC allows each device to raise at most one
		// interrupt at a time; tell the PLIC the device is
		// now allowed to interrupt again.
		// 再次放行中断
		/**
		 * plic_claim 阶段，PLIC 会锁定对应外设的中断网关以防止重复触发。只有
		 * 当收到 plic_complete 消息后，网关才会被释放，重新允许该外设产生新
		 * 的中断请求.
		 * plic_complete 必须与 plic_claim 严格配对使用:
		 * - Claim（认领）：进入中断时，读取 Claim 寄存器获取中断 ID，PLIC
		 *   自动屏蔽该中断源，防止重入;
		 * - Complete（完成）：退出中断时，向 Complete 寄存器写入相同的 ID，
		 *   告诉 PLIC 可以重新放行该中断;
		 */
		if (irq)
			plic_complete(irq);

		return 1;
	} else if (scause == 0x8000000000000001L) {
		// software interrupt from a machine-mode timer interrupt,
		// forwarded by timervec in kernelvec.S.

		// 每个核心都触发硬件时钟中断，但只有 cpu0 处理对应的软中断
		if (cpuid() == 0) {
			clockintr();
		}

		// acknowledge the software interrupt by clearing
		// the SSIP bit in sip.
		w_sip(r_sip() & ~2);

		return 2;
	} else {
		// 其他类型的中断
		return 0;
	}
}
