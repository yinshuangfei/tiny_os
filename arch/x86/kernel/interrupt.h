#ifndef __INTERRUPT_H__
#define __INTERRUPT_H__

#include "types.h"

#define IRQ_TIMER		0x20	// Timer Interrupt
#define EXC_DIVIDE_ERROR	0x00	// Divide Error
#define EXC_PAGE_FAULT		0x0e	// Page Fault
#define INT_SYSCALL		0x80	// System Call

struct trapframe {
	uint32 edi;
	uint32 esi;
	uint32 ebp;
	uint32 oesp;
	uint32 ebx;
	uint32 edx;
	uint32 ecx;
	uint32 eax;
	uint32 ds;
	uint32 err;
	uint32 eip;
	uint32 cs;
	uint32 eflags;
};

#endif