#include "types.h"
#include "defs.h"
#include "gdt.h"
#include "param.h"
#include "mm/memlayout.h"
#include "x86.h"
#include "mp.h"

static struct gdt_entry gdt[GDT_ENTRIES] __attribute__((aligned(8)));
static struct tss tss[NR_CPUS] __attribute__((aligned(16)));

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

static void tss_load(int cpu)
{
	uint16 sel = (uint16)SEG_TSS(cpu);

	__asm__ volatile ("ltr %w0" : : "r"(sel));
}

static void gdt_load_common(void)
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
	int i;
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
	for (i = 0; i < NR_CPUS; i++)
		tss_init_cpu(i);

	gdt_load_common();
	tss_load(0);
	printk(KERN_INFO "gdt: system gdt loaded (%d TSS)\n", NR_CPUS);
}

/* AP：已在分页后，只需 lgdt + ltr（段寄存器已是内核选择子） */
void gdt_load_ap(int cpu)
{
	struct gdt_ptr ptr;

	ptr.limit = sizeof(gdt) - 1;
	ptr.base = (uint32)gdt;
	__asm__ volatile ("lgdt %0" : : "m"(ptr) : "memory");
	tss_load(cpu);
}

void tss_init_cpu(int cpu)
{
	uint32 base;
	uint32 limit;
	int idx;

	if (cpu < 0 || cpu >= NR_CPUS)
		return;

	memset(&tss[cpu], 0, sizeof(tss[cpu]));
	tss[cpu].ss0 = SEG_KDATA;
	tss[cpu].esp0 = interrupt_stack_tops[cpu]
		? interrupt_stack_tops[cpu]
		: (uint32)&interrupt_stacks[cpu][0] + KSTACKSIZE;
	tss[cpu].iomap_base = sizeof(struct tss);

	base = (uint32)&tss[cpu];
	limit = sizeof(struct tss) - 1;
	idx = 5 + cpu;
	gdt_set_entry(idx, base, limit, GDT_TSS, (limit >> 16) & 0x0f);
}

void tss_init(void)
{
	tss_init_cpu(0);
}

void tss_set_esp0(uint32 esp)
{
	int id = cpu_id();

	if (id < 0 || id >= NR_CPUS)
		id = 0;
	tss[id].esp0 = esp;
}

uint32 tss_get_esp0(void)
{
	int id = cpu_id();

	if (id < 0 || id >= NR_CPUS)
		id = 0;
	return tss[id].esp0;
}

uint32 tss_get_ss0(void)
{
	return SEG_KDATA;
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
