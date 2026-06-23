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
	w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void usertrap(void)
{
	printf("usertrap\n");
	int which_dev = 0;

	if ((r_sstatus() & SSTATUS_SPP) != 0)
		panic("usertrap: not from user mode");

	// send interrupts and exceptions to kerneltrap(),
	// since we're now in the kernel.
	w_stvec((uint64)kernelvec);

	struct proc *p = myproc();

	// save user program counter.
	p->trapframe->epc = r_sepc();

	if (r_scause() == 8) {
		// system call

		// if (p->killed)
		// 	exit(-1);

		// sepc points to the ecall instruction,
		// but we want to return to the next instruction.
		p->trapframe->epc += 4;

		// an interrupt will change sstatus &c registers,
		// so don't enable until done with those registers.
		intr_on();

		// syscall();
	} else if ((which_dev = devintr()) != 0) {
		// ok
	} else {
		printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
		printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
		p->killed = 1;
	}

	// if (p->killed)
	// 	exit(-1);

	// give up the CPU if this is a timer interrupt.
	if (which_dev == 2)
		yield();

	usertrapret();
}

//
// return to user space
//
void usertrapret(void)
{
	printf("usertrapret\n");
	struct proc *p = myproc();

	// we're about to switch the destination of traps from
	// kerneltrap() to usertrap(), so turn off interrupts until
	// we're back in user space, where usertrap() is correct.
	intr_off();
	printf("usertrapret2\n");

	// send syscalls, interrupts, and exceptions to trampoline.S
	w_stvec(TRAMPOLINE + (uservec - trampoline));

	// set up trapframe values that uservec will need when
	// the process next re-enters the kernel.
	p->trapframe->kernel_satp = r_satp();         // kernel page table
	p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
	p->trapframe->kernel_trap = (uint64)usertrap;
	p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

	// set up the registers that trampoline.S's sret will use
	// to get to user space.

	// set S Previous Privilege mode to User.
	unsigned long x = r_sstatus();
	x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
	x |= SSTATUS_SPIE; // enable interrupts in user mode
	w_sstatus(x);

	// set S Exception Program Counter to the saved user pc.
	w_sepc(p->trapframe->epc);

	// tell trampoline.S the user page table to switch to.
	uint64 satp = MAKE_SATP(p->pagetable);

	// jump to trampoline.S at the top of memory, which
	// switches to the user page table, restores user registers,
	// and switches to user mode with sret.
	uint64 fn = TRAMPOLINE + (userret - trampoline);
	((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
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
	if (which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
		yield();

	// the yield() may have caused some traps to occur,
	// so restore trap registers for use by kernelvec.S's sepc instruction.
	w_sepc(sepc);
	w_sstatus(sstatus);
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
		// this is a supervisor external interrupt, via PLIC.

		// irq indicates which device interrupted.
		// 调用 plic_claim() 后，intr_on() 函数才不会卡住
		int irq = plic_claim();

		if (irq == UART0_IRQ) {
			uartintr();
			// printf("cpu:%d, uartintr interrupt irq=%d\n", cpuid(), irq);
		} else if (irq == VIRTIO0_IRQ) {
			// virtio_disk_intr();
			printf("virtio_disk_intr interrupt irq=%d\n", irq);
		} else if (irq) {
			printf("unexpected interrupt irq=%d\n", irq);
		}

		// the PLIC allows each device to raise at most one
		// interrupt at a time; tell the PLIC the device is
		// now allowed to interrupt again.
		if (irq)
			plic_complete(irq);

		return 1;
	} else if (scause == 0x8000000000000001L) {
		// software interrupt from a machine-mode timer interrupt,
		// forwarded by timervec in kernelvec.S.

		if (cpuid() == 0) {
			// clockintr();
			printf("clockintr interrupt\n");
		}

		printf("timer interrupt come ...\n");

		// acknowledge the software interrupt by clearing
		// the SSIP bit in sip.
		w_sip(r_sip() & ~2);

		return 2;
	} else {
		return 0;
	}
}
