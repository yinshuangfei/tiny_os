#include "x86.h"
#include "defs.h"
#include "interrupt.h"

/** PIC: Programmable Interrupt Controller (可编程中断控制器)  */
/** 8259 主中断控制器 */
#define PIC1_CMD  0x20		// 命令端口
#define PIC1_DATA 0x21		// 数据端口
/** 8259 从中断控制器 */
#define PIC2_CMD  0xa0
#define PIC2_DATA 0xa1

/**
 * 8259 EOI：IRQ >= 8 时需先向从片、再向主片发送。
 * 在 handler 开头调用，避免 handler 偏长时边沿触发丢下一拍 IRQ。
 */
void pic_eoi(int irq)
{
	// 清 ISR、允许 PIC 再报这条/后续 IRQ
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
	// 打开 IRQ0(定时器)、IRQ1(键盘)、IRQ4(COM1)；其余屏蔽
	// mask = 1110 1100 → 0xec
	outb(PIC1_DATA, 0xec);
	outb(PIC2_DATA, 0xff);

	// TODO: 选择性配置 IOAPIC/LAPIC
	printk(KERN_INFO "pic: system pic(8259) initialized\n");
}