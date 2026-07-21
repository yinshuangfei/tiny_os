#include "x86.h"
#include "defs.h"
#include "interrupt.h"
#include "lapic.h"
#include "ioapic.h"
#include "idt.h"
#include "timer.h"

/** PIC: Programmable Interrupt Controller (可编程中断控制器)  */
/** 8259 主中断控制器 */
#define PIC1_CMD  0x20		// 命令端口
#define PIC1_DATA 0x21		// 数据端口
/** 8259 从中断控制器 */
#define PIC2_CMD  0xa0
#define PIC2_DATA 0xa1

static int use_apic;		/* 1：中断经 IOAPIC/LAPIC；0：8259 */

/**
 * 8259 EOI：IRQ >= 8 时需先向从片、再向主片发送。
 */
void pic_eoi(int irq)
{
	if (irq >= 8)
		outb(PIC2_CMD, 0x20);
	outb(PIC1_CMD, 0x20);
}

/* 屏蔽全部 8259 IRQ（改用 IOAPIC 时调用） */
void pic_disable(void)
{
	outb(PIC1_DATA, 0xff);
	outb(PIC2_DATA, 0xff);
}

/*
 * 统一 EOI：APIC 路径写 LAPIC EOI；否则写 8259。
 * 设备 handler 应调用本函数，勿直接 pic_eoi。
 */
void irq_eoi(int irq)
{
	if (use_apic)
		lapic_eoi();
	else
		pic_eoi(irq);
}

int irq_using_apic(void)
{
	return use_apic;
}

/**
 * 配置 8259：重映射向量，默认打开常用 IRQ（无 APIC 时的回退路径）。
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
	/*
	 * 主片：开 IRQ0/1/2/4；从片：开 IRQ10/11/14/15
	 * apic_init 成功后会 pic_disable() 全屏蔽。
	 */
	outb(PIC1_DATA, 0xe8);
	outb(PIC2_DATA, 0x33);

	printk(KERN_INFO "pic: system pic(8259) initialized\n");
}

/*
 * 启用 Local APIC + I/O APIC。
 * 时钟：LAPIC timer → 向量 0x20（不用 PIT/IOAPIC IRQ0）。
 * 其它 ISA IRQ：IOAPIC → 0x20+irq；8259 全屏蔽。
 */
int apic_init(void)
{
	int apic_id;

	if (!lapic_present()) {
		printk(KERN_INFO "apic: CPU has no local APIC, keep 8259\n");
		return -1;
	}

	lapic_map();
	ioapic_map();

	if (lapic_init() < 0)
		return -1;
	if (ioapic_init() < 0)
		return -1;

	/* MPS IMCR：中断源切到 APIC（QEMU 上通常无害） */
	outb(0x22, 0x70);
	outb(0x23, inb(0x23) | 1);

	pic_disable();

	/* 周期 tick；替代 PIT IRQ0 */
	lapic_timer_init(TIMER_HZ);

	apic_id = (int)lapic_id();
	/* 不路由 IRQ0：时钟已由 LAPIC timer 提供 */
	ioapic_enable(IRQ_1_KEYBOARD, apic_id);
	ioapic_enable(IRQ_4_COM1, apic_id);
	ioapic_enable(IRQ_10_IDE1B, apic_id);
	ioapic_enable(IRQ_11_IDE1A, apic_id);
	ioapic_enable(IRQ_14_IDE0, apic_id);
	ioapic_enable(IRQ_15_IDE1, apic_id);

	use_apic = 1;
	printk(KERN_INFO
	       "apic: LAPIC timer + IOAPIC IRQs -> 0x%x+, PIC masked\n",
	       IRQ_TIMER);
	return 0;
}
