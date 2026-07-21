#ifndef __GDT_H__
#define __GDT_H__

#include "types.h"
#include "param.h"
/* DPL: Descriptor Privilege Level */
#define DPL_KERNEL		0	// 内核态
#define DPL_USER		3	// 用户态

/* GDT 段选择子（index << 3） */
#define SEG_KCODE		0x08	// 内核代码段，DPL=0
#define SEG_KDATA		0x10	// 内核数据段，DPL=0
#define SEG_UCODE		0x18	// 用户代码段，DPL=3
#define SEG_UDATA		0x20	// 用户数据段，DPL=3
/* TSS：GDT index 5+i → 选择子 0x28+i*8 */
#define SEG_TSS(i)		(0x28 + ((i) << 3))

/* GDT access 字节：P=1, S=1, Type/DPL 因段类型而异 */
#define GDT_KERNEL_CODE		0x9a	// 可执行、可读，DPL=0
#define GDT_KERNEL_DATA		0x92	// 可读写，DPL=0
#define GDT_USER_CODE		0xfa	// 可执行、可读，DPL=3
#define GDT_USER_DATA		0xf2	// 可读写，DPL=3
#define GDT_TSS			0x89	// 可用 32 位 TSS（系统段，P=1, DPL=0, Type=1001）

/* granularity：G=1, D/B=1, limit[19:16]=0xF → 4GB 平坦段 */
#define GDT_GRAN_4GB		0xcf
#define GDT_ACCESS_MASK		0xfe

#define GDT_ENTRIES		(5 + NR_CPUS)

struct gdt_entry {
	uint16 limit_low;	// 段界限 0-15
	uint16 base_low;	// 基地址 0-15
	uint8 base_middle;	// 基地址 16-23
	uint8 access;		// 访问权限 (Present, DPL, S, Type)
	uint8 granularity;	// 粒度与标志位 (Limit 16-19, AVL, L, D/B, G)
	uint8 base_high;	// 基地址 24-31
} __attribute__((packed));

struct gdt_ptr {
	uint16 limit;
	uint32 base;
} __attribute__((packed));

/* 32 位 TSS（104 字节）；硬件任务切换已弃用，esp0/ss0 供 ring3→ring0 用 */
struct tss {
	uint32 link;
	uint32 esp0;		/* init 指向中断处理栈的栈顶, 后面设置为进程的内核栈栈顶 */
	uint32 ss0;		/* 内核栈段为数据段 SEG_KDATA */
	uint32 esp1;
	uint32 ss1;
	uint32 esp2;
	uint32 ss2;
	uint32 cr3;
	uint32 eip;
	uint32 eflags;
	uint32 eax;
	uint32 ecx;
	uint32 edx;
	uint32 ebx;
	uint32 esp;
	uint32 ebp;
	uint32 esi;
	uint32 edi;
	uint32 es;
	uint32 cs;
	uint32 ss;
	uint32 ds;
	uint32 fs;
	uint32 gs;
	uint32 ldt;
	uint16 trap;
	uint16 iomap_base;	/* I/O 位图偏移 */
} __attribute__((packed));

uint32 gdt_table_addr(void);
uint16 gdt_table_limit(void);
uint8 gdt_get_access(int idx);
uint8 gdt_get_granularity(int idx);

void gdt_init(void);
void gdt_load_ap(int cpu);
void tss_init(void);
void tss_init_cpu(int cpu);
void tss_set_esp0(uint32 esp);
uint32 tss_get_esp0(void);
uint32 tss_get_ss0(void);

#endif
