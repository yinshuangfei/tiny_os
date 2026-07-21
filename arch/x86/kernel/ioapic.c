/*
 * I/O APIC 驱动（教学）：固定基址 0xFEC00000，无 MADT。
 * 重定向表：边沿触发、高电平有效、Fixed 投递到指定 LAPIC ID。
 */
#include "types.h"
#include "defs.h"
#include "ioapic.h"
#include "idt.h"
#include "mm/mmu.h"
#include "mm/vm.h"
#include "mm/memlayout.h"
#include "lapic.h"

#define REG_ID		0x00	/* IOAPIC ID Register */
#define REG_VER		0x01	/* IOAPIC Version Register */
#define REG_TABLE	0x10	/* 重定向表起始（每 IRQ 占 2 个 32 位寄存器） */

/* 重定向低 32 位 */
#define IOAPIC_MASK		0x00010000	/* bit16：屏蔽 */
#define IOAPIC_TRIG_LEVEL 	0x00008000	/* 电平触发（ISA 用边沿=0） */
#define IOAPIC_POL_LOW		0x00002000	/* 低有效（ISA 用高有效=0） */
#define IOAPIC_DEST_LOG		0x00000800	/* 逻辑目标（物理=0） */
#define IOAPIC_DELIV_FIXED 	0x00000000

static volatile uint32 *ioapic;	/* IOAPIC 基地址 */
static int ioapic_ok;		/* IOAPIC 是否初始化成功 */
static int ioapic_maxintr;	/* Maximum Interrupt Vector */

static uint32 ioapic_read(int reg)
{
	ioapic[0] = (uint32)reg;	/* IOREGSEL */
	return ioapic[4];		/* IOWIN at +0x10 → index 4 of uint32* */
}

static void ioapic_write(int reg, uint32 data)
{
	ioapic[0] = (uint32)reg;
	ioapic[4] = data;
}

void ioapic_map(void)
{
	kvmmap(IOAPIC_DEFAULT_BASE, IOAPIC_DEFAULT_BASE, PGSIZE,
	       PTE_W | PTE_PCD | PTE_PWT);
	ioapic = (volatile uint32 *)IOAPIC_DEFAULT_BASE;
}

int ioapic_init(void)
{
	uint32 ver;

	if (!ioapic) {
		printk(KERN_ERR "ioapic: not mapped\n");
		return -1;
	}

	ver = ioapic_read(REG_VER);
	ioapic_maxintr = ((ver >> 16) & 0xff);
	ioapic_ok = 1;
	printk(KERN_INFO "ioapic: id=%u ver=%u maxintr=%d at %p\n",
	       ioapic_read(REG_ID) >> 24, ver & 0xff, ioapic_maxintr, ioapic);
	return 0;
}

/*
 * 使能 irq：向量 = IRQ_TIMER(0x20) + irq，投递到 cpunum（LAPIC ID）。
 * 边沿、高有效、Fixed、物理模式。
 */
void ioapic_enable(int irq, int cpunum)
{
	uint32 lo, hi;

	if (!ioapic_ok || irq < 0 || irq > ioapic_maxintr)
		return;

	lo = (uint32)(IRQ_TIMER + irq);
	lo |= IOAPIC_DELIV_FIXED;
	/* 不置 MASK → 使能 */
	hi = ((uint32)cpunum) << 24;

	ioapic_write(REG_TABLE + 2 * irq, lo);
	ioapic_write(REG_TABLE + 2 * irq + 1, hi);
}
