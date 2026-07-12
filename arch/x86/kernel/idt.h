#ifndef __IDT_H__
#define __IDT_H__

#include "types.h"

#define IRQ_TIMER		0x20	// Timer Interrupt
#define EXC_DIVIDE_ERROR	0x00	// Divide Error
#define EXC_INVALID_OPCODE	0x06	// #UD Invalid Opcode
#define EXC_DEVICE_NOT_AVAILABLE 0x07	// #NM Device Not Available
#define EXC_GENERAL_PROTECTION	0x0d	// #GP General Protection
#define EXC_PAGE_FAULT		0x0e	// Page Fault
#define EXC_SIMD_FP		0x13	// #XM SIMD FP Exception
#define INT_SYSCALL		0x80	// System Call

/* IDT 门属性：P=1, DPL, 0, E=1, Gate=1110(中断门) */
#define IDT_ATTR_KERNEL		0x8e	// DPL=0，内核中断/异常
#define IDT_ATTR_USER		0xee	// DPL=3，用户可触发（如 int 0x80）

#define IDT_ENTRIES		256

struct idt_entry {
	uint16 offset_low;
	uint16 selector;
	uint8 zero;
	uint8 type_attr;
	uint16 offset_high;
} __attribute__((packed));

struct idt_ptr {
	uint16 limit;
	uint32 base;
} __attribute__((packed));

uint32 idt_table_addr(void);
uint16 idt_table_limit(void);
uint32 idt_get_handler(int vec);
uint16 idt_get_selector(int vec);
uint8 idt_get_type_attr(int vec);

void idt_set_gate(int vec, void (*handler)(void), uint16 selector, uint8 type_attr);
void idt_init(void);

#endif
