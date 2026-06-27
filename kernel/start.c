#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "types.h"

void main();
void timerinit();

// entry.S needs one stack per CPU.
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// assembly code in kernelvec.S for machine-mode timer interrupt.
extern void timervec();

void start()
{
	// set M Previous Privilege mode to Supervisor, for mret.
	unsigned long x = r_mstatus();
	x &= ~MSTATUS_MPP_MASK;
	x |= MSTATUS_MPP_S;
	w_mstatus(x);

	// set M Exception Program Counter to main, for mret.
	// requires gcc -mcmodel=medany
	w_mepc((uint64)main);

	// disable paging for now.
	w_satp(0);

	// delegate all interrupts and exceptions to supervisor mode.
	// 委托所有中断和异常给内核态
	w_medeleg(0xffff);
	w_mideleg(0xffff);
	w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

	// ask for clock interrupts.
	// timerinit();

	// keep each CPU's hartid in its tp register, for cpuid().
	int id = r_mhartid();
	w_tp(id);

	// switch to supervisor mode and jump to main().
	// 从机器模式(Machine Mode)切换到监控模式（Supervisor-mode）
	asm volatile("mret");
}

// set up to receive timer interrupts in machine mode,
// which arrive at timervec in kernelvec.S,
// which turns them into software interrupts for
// devintr() in trap.c.
void timerinit()
{
	// each CPU has a separate source of timer interrupts.
	// int id = r_mhartid();

	// ask the CLINT for a timer interrupt.
	// int interval = 1000000; // cycles; about 1/10th second in qemu.
	// *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + interval;

	// prepare information in scratch[] for timervec.
	// scratch[0..3] : space for timervec to save registers.
	// scratch[4] : address of CLINT MTIMECMP register.
	// scratch[5] : desired interval (in cycles) between timer interrupts.
	// uint64 *scratch = &mscratch0[32 * id];
	// scratch[4] = CLINT_MTIMECMP(id);
	// scratch[5] = interval;
	// w_mscratch((uint64)scratch);

	// set the machine-mode trap handler.
	w_mtvec((uint64)timervec);

	// enable machine-mode interrupts.
	w_mstatus(r_mstatus() | MSTATUS_MIE);

	// enable machine-mode timer interrupts.
	w_mie(r_mie() | MIE_MTIE);
}
