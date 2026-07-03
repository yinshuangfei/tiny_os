#include "types.h"

struct idt_entry {
	ushort offset_low;	/* 偏移量低 16 位 */
	ushort selector;	/* 选择子 */
	uint8 zero;		/* 总是 0 */
	uint8 type_attr;	/* 类型和属性 */
	ushort offset_high;	/* 偏移量高 16 位 */
} __attribute__((packed));

struct idt_ptr {
	ushort limit;		/* 限长 */
	uint base;		/* 基址 */
} __attribute__((packed));

struct idt_entry idt[256];

void idt_set_gate(int vec, void (*handler)(void))
{
	uint addr = (uint)handler;

	idt[vec].offset_low = addr & 0xffff;
	idt[vec].selector = 0x08;
	idt[vec].zero = 0;
	idt[vec].type_attr = 0x8e;
	idt[vec].offset_high = (addr >> 16) & 0xffff;
}

void idt_load(void)
{
	struct idt_ptr ptr;

	ptr.limit = sizeof(idt) - 1;
	ptr.base = (uint)idt;
	__asm__ volatile ("lidt %0" : : "m"(ptr));
}
