#include "types.h"
#include "defs.h"
#include "gdt.h"
#include "idt.h"
#include "x86.h"

struct idt_entry idt[IDT_ENTRIES];

extern void isr_timer(void);
extern void isr_kbd(void);
extern void isr_serial(void);
extern void isr_ide10(void);
extern void isr_ide11(void);
extern void isr_ide14(void);
extern void isr_ide15(void);
extern void isr_divide_error(void);
extern void isr_device_not_available(void);
extern void isr_invalid_opcode(void);
extern void isr_general_protection(void);
extern void isr_page_fault(void);
extern void isr_simd_fp(void);
extern void isr_syscall(void);

void idt_set_gate(int vec, void (*handler)(void), uint16 selector, uint8 type_attr)
{
	uint32 addr = (uint32)handler;

	idt[vec].offset_low = addr & 0xffff;
	idt[vec].selector = selector;
	idt[vec].zero = 0;
	idt[vec].type_attr = type_attr;
	idt[vec].offset_high = (addr >> 16) & 0xffff;
}

static void idt_load(void)
{
	struct idt_ptr ptr;

	ptr.limit = sizeof(idt) - 1;
	ptr.base = (uint32)idt;

	// 加载 IDT 寄存器, 告诉 CPU 中断描述符表（IDT）在内存中的具体位置和大小
	__asm__ volatile ("lidt %0" : : "m"(ptr));
}

void idt_init(void)
{
	// ISR 是 Interrupt Service Routine
	idt_set_gate(EXC_DIVIDE_ERROR, isr_divide_error, SEG_KCODE, IDT_ATTR_KERNEL);
	idt_set_gate(EXC_DEVICE_NOT_AVAILABLE, isr_device_not_available, SEG_KCODE, IDT_ATTR_KERNEL);
	idt_set_gate(EXC_INVALID_OPCODE, isr_invalid_opcode, SEG_KCODE, IDT_ATTR_KERNEL);
	idt_set_gate(EXC_GENERAL_PROTECTION, isr_general_protection, SEG_KCODE, IDT_ATTR_KERNEL);
	idt_set_gate(EXC_PAGE_FAULT, isr_page_fault, SEG_KCODE, IDT_ATTR_KERNEL);
	idt_set_gate(EXC_SIMD_FP, isr_simd_fp, SEG_KCODE, IDT_ATTR_KERNEL);
	idt_set_gate(IRQ_TIMER, isr_timer, SEG_KCODE, IDT_ATTR_KERNEL);
	idt_set_gate(IRQ_KBD, isr_kbd, SEG_KCODE, IDT_ATTR_KERNEL);
	idt_set_gate(IRQ_COM1, isr_serial, SEG_KCODE, IDT_ATTR_KERNEL);
	idt_set_gate(IRQ_IDE1B, isr_ide10, SEG_KCODE, IDT_ATTR_KERNEL);
	idt_set_gate(IRQ_IDE1A, isr_ide11, SEG_KCODE, IDT_ATTR_KERNEL);
	idt_set_gate(IRQ_IDE0, isr_ide14, SEG_KCODE, IDT_ATTR_KERNEL);
	idt_set_gate(IRQ_IDE1, isr_ide15, SEG_KCODE, IDT_ATTR_KERNEL);
	idt_set_gate(INT_SYSCALL, isr_syscall, SEG_KCODE, IDT_ATTR_USER);	// DPL=3，用户态可 int 0x80

	idt_load();
	printk(KERN_INFO "idt: system idt loaded\n");
}

uint32 idt_table_addr(void)
{
	return (uint32)idt;
}

uint16 idt_table_limit(void)
{
	return sizeof(idt) - 1;
}

uint32 idt_get_handler(int vec)
{
	return idt[vec].offset_low | ((uint32)idt[vec].offset_high << 16);
}

uint16 idt_get_selector(int vec)
{
	return idt[vec].selector;
}

uint8 idt_get_type_attr(int vec)
{
	return idt[vec].type_attr;
}
