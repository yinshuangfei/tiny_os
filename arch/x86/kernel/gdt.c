#include "types.h"
#include "defs.h"
#include "gdt.h"
#include "memlayout.h"
#include "x86.h"

static struct gdt_entry gdt[GDT_ENTRIES] __attribute__((aligned(8)));
static struct tss tss __attribute__((aligned(16)));

static void gdt_set_entry(int idx, uint32 base, uint32 limit,
			  uint8 access, uint8 gran)
{
	gdt[idx].limit_low = limit & 0xffff;
	gdt[idx].base_low = base & 0xffff;
	gdt[idx].base_middle = (base >> 16) & 0xff;
	gdt[idx].access = access;
	gdt[idx].granularity = gran;
	gdt[idx].base_high = (base >> 24) & 0xff;
}

static void tss_load(void)
{
	__asm__ volatile ("ltr %w0" : : "r"(SEG_TSS));
}

static void gdt_load(void)
{
	struct gdt_ptr ptr;

	ptr.limit = sizeof(gdt) - 1;
	ptr.base = (uint32)gdt;

	/*
	 * lgdt 只更新 GDTR, 不会自动改 CS、DS、SS 等段寄存器里缓存的选择子；
	 * 段寄存器仍缓存旧值，必须显式重载。
	 * CS 需 ljmp 远跳转，其余段寄存器 mov 即可。
	 */
	__asm__ volatile (
		"lgdt %0\n"
		"movw %1, %%ax\n"
		"movw %%ax, %%ds\n"
		"movw %%ax, %%es\n"
		"movw %%ax, %%fs\n"
		"movw %%ax, %%gs\n"
		"movw %%ax, %%ss\n"
		"ljmp %2, $1f\n"
		"1:\n"
		:
		: "m"(ptr), "i"(SEG_KDATA), "i"(SEG_KCODE)
		: "ax", "memory");
}

void gdt_init(void)
{
	/* 0: 空描述符 */
	gdt_set_entry(0, 0, 0, 0, 0);
	/* 1: 内核代码段 */
	gdt_set_entry(1, 0, 0xffff, GDT_KERNEL_CODE, GDT_GRAN_4GB);
	/* 2: 内核数据段 */
	gdt_set_entry(2, 0, 0xffff, GDT_KERNEL_DATA, GDT_GRAN_4GB);
	/* 3: 用户代码段 */
	gdt_set_entry(3, 0, 0xffff, GDT_USER_CODE, GDT_GRAN_4GB);
	/* 4: 用户数据段 */
	gdt_set_entry(4, 0, 0xffff, GDT_USER_DATA, GDT_GRAN_4GB);
	/* 5: TSS（须在 lgdt 之前写入 GDT） */
	tss_init();

	gdt_load();
	tss_load();
	printk(KERN_INFO "gdt: system gdt loaded\n");
}

void tss_init(void)
{
	uint32 base = (uint32)&tss;
	uint32 limit = sizeof(tss) - 1;

	memset(&tss, 0, sizeof(tss));
	tss.ss0 = SEG_KDATA;
	tss.esp0 = (uint32)INTERRUPT_STACK_TOP;
	tss.iomap_base = sizeof(tss);

	gdt_set_entry(5, base, limit, GDT_TSS, (limit >> 16) & 0x0f);
}

void tss_set_esp0(uint32 esp)
{
	tss.esp0 = esp;
}

uint32 tss_get_esp0(void)
{
	return tss.esp0;
}

uint32 tss_get_ss0(void)
{
	return tss.ss0;
}

uint32 gdt_table_addr(void)
{
	return (uint32)gdt;
}

uint16 gdt_table_limit(void)
{
	return sizeof(gdt) - 1;
}

uint8 gdt_get_access(int idx)
{
	return gdt[idx].access;
}

uint8 gdt_get_granularity(int idx)
{
	return gdt[idx].granularity;
}
