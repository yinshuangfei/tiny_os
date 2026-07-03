struct idt_entry {
	unsigned short offset_low;
	unsigned short selector;
	unsigned char zero;
	unsigned char type_attr;
	unsigned short offset_high;
} __attribute__((packed));

struct idt_ptr {
	unsigned short limit;
	unsigned int base;
} __attribute__((packed));

struct idt_entry idt[256];

void idt_set_gate(int vec, void (*handler)(void))
{
	unsigned int addr = (unsigned int)handler;

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
	ptr.base = (unsigned int)idt;
	__asm__ volatile ("lidt %0" : : "m"(ptr));
}