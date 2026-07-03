#include "types.h"
#include "defs.h"
#include "interrupt.h"
#include "x86.h"

struct idt_entry {
	uint16 offset_low;	/* 偏移量低 16 位 */
	uint16 selector;	/* 选择子 */
	uint8 zero;		/* 总是 0 */
	uint8 type_attr;	/* 类型和属性 */
	uint16 offset_high;	/* 偏移量高 16 位 */
} __attribute__((packed));

struct idt_ptr {
	uint16 limit;		/* 限长 */
	uint32 base;		/* 基址 */
} __attribute__((packed));

#define IDT_ENTRIES 256
struct idt_entry idt[IDT_ENTRIES];

extern void isr_timer(void);
extern void isr_divide_error(void);
extern void isr_page_fault(void);
extern void isr_syscall(void);

void page_fault_handler(struct trapframe *tf)
{
	uint32 fault_va;

	__asm__ volatile ("movl %%cr2, %0" : "=r"(fault_va));
	printf("page fault at va=%p err=0x%x eip=0x%x\n",
	       (void *)fault_va, tf->err, tf->eip);
	panic("page fault");
}

void idt_set_gate(int vec, void (*handler)(void), uint16 selector, uint8 type_attr)
{
	uint32 addr = (uint32)handler;

	idt[vec].offset_low = addr & 0xffff;
	idt[vec].selector = selector;
	idt[vec].zero = 0;
	idt[vec].type_attr = type_attr;
	idt[vec].offset_high = (addr >> 16) & 0xffff;
}

void idt_load(void)
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
	idt_set_gate(EXC_DIVIDE_ERROR, isr_divide_error, 0x08, 0x8e);
	idt_set_gate(EXC_PAGE_FAULT, isr_page_fault, 0x08, 0x8e);
	idt_set_gate(IRQ_TIMER, isr_timer, 0x08, 0x8e);
	idt_set_gate(INT_SYSCALL, isr_syscall, 0x08, 0xee);	// DPL=3，用户态可 int 0x80

	idt_load();
}