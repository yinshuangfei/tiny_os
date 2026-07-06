#include "defs.h"
#include "x86.h"
#include "timer.h"
#include "proc.h"

/** PIC: Programmable Interrupt Controller (可编程中断控制器)  */
/** 8259 主中断控制器 */
#define PIC1_CMD  0x20		// 命令端口
#define PIC1_DATA 0x21		// 数据端口
/** 8259 从中断控制器 */
#define PIC2_CMD  0xa0
#define PIC2_DATA 0xa1

/** PIT: Programmable Interval Timer (可编程间隔定时器) */
/** 产生定时器中断 IRQ0 (Timer Tick) */
#define PIT_CMD   0x43		// 命令端口
#define PIT_CH0   0x40		// 通道 0 端口

#define PIT_FREQ  1193182	/* 8254 输入时钟 1.193182 MHz */

static volatile unsigned int ticks;

/**
 * 8259 EOI：IRQ >= 8 时需先向从片、再向主片发送。
 * 在 handler 开头调用，避免 handler 偏长时边沿触发丢下一拍 IRQ。
 */
void pic_eoi(int irq)
{
	if (irq >= 8)
		outb(PIC2_CMD, 0x20);
	outb(PIC1_CMD, 0x20);
}

/**
 * 配置 8259 中断控制器
 * 8259 是主从级联两片：主片管 IRQ0–7，从片管 IRQ8–15
*/
void pic_init(void)
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

	// TODO: 选择性配置 IOAPIC/LAPIC
	printf("system pic(8259) initialized\n");
}

/** 配置 8254 可编程间隔定时器 */
void pit_init(void)
{
	uint32 divisor = PIT_FREQ / TIMER_HZ;

	if (divisor > 0xffff)
		panic("pit: divisor overflow");

	/**
	 * bit 7–6: 通道选择：00 通道 0，01 通道 1，10 通道 2，11 读回波特率
	 * bit 5–4: 存取模式：00 方式 0，01 方式 1，10 方式 2，11 方式 3 (方波发生器)
	 * bit 3–0: 存取频率：二进制计数，BCD 计数
	 * 0x36 = 0011 0110
	 * 00: 通道 0
	 * 11: 方式 3
	 * 0110: 二进制计数（非 BCD）
	 */
	outb(PIT_CMD, 0x36);
	outb(PIT_CH0, divisor & 0xff);
	outb(PIT_CH0, (divisor >> 8) & 0xff);

	printf("system pit(8254) initialized, timer frequency: %d Hz\n", TIMER_HZ);
}

void timer_handler(void)
{
	/* IRQ0：先 EOI 再干活，避免 8259 边沿模式下 handler 过长丢中断 */
	pic_eoi(0);
	ticks++;
	proc_timer_tick();
}

unsigned int timer_ticks(void)
{
	return ticks;
}
