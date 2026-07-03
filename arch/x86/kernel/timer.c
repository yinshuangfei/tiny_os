#include "defs.h"
#include "x86.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xa0
#define PIC2_DATA 0xa1

#define PIT_CMD   0x43
#define PIT_CH0   0x40

#define PIT_FREQ  1193182
#define TIMER_HZ  100
#define IRQ_TIMER 0x20

extern void timervec(void);

static volatile unsigned int ticks;

static void pic_init(void)
{
	// ICW1: 级联、边沿触发、需要 ICW4
	outb(PIC1_CMD, 0x11);
	outb(PIC2_CMD, 0x11);
	// ICW2: 重映射 IRQ0-7 -> INT 0x20-0x27
	outb(PIC1_DATA, 0x20);
	outb(PIC2_DATA, 0x28);
	// ICW3: 主片 IRQ2 接从片
	outb(PIC1_DATA, 0x04);
	outb(PIC2_DATA, 0x02);
	// ICW4: 8086 模式
	outb(PIC1_DATA, 0x01);
	outb(PIC2_DATA, 0x01);
	// 仅打开定时器 IRQ0
	outb(PIC1_DATA, 0xfe);
	outb(PIC2_DATA, 0xff);
}

static void pit_init(void)
{
	unsigned short divisor = PIT_FREQ / TIMER_HZ;

	outb(PIT_CMD, 0x36);
	outb(PIT_CH0, divisor & 0xff);
	outb(PIT_CH0, (divisor >> 8) & 0xff);
}

void timer_handler(void)
{
	ticks++;
}

unsigned int timer_ticks(void)
{
	return ticks;
}

void timer_init(void)
{
	idt_set_gate(IRQ_TIMER, timervec);
	idt_load();
	pic_init();
	pit_init();
	sti();
}
