#ifndef __INTERRUPT_H__
#define __INTERRUPT_H__

#include "types.h"

#define IRQ_TIMER		0x20	// Timer Interrupt
#define EXC_DIVIDE_ERROR	0x00	// Divide Error
#define EXC_INVALID_OPCODE	0x06	// #UD Invalid Opcode
#define EXC_DEVICE_NOT_AVAILABLE 0x07	// #NM Device Not Available
#define EXC_GENERAL_PROTECTION	0x0d	// #GP General Protection
#define EXC_PAGE_FAULT		0x0e	// Page Fault
#define EXC_SIMD_FP		0x13	// #XM SIMD FP Exception
#define INT_SYSCALL		0x80	// System Call

/* IDT 门属性：P=1, DPL, 0, E=1, Gate=1110(中断门) */
#define IDT_ATTR_KERNEL		0x8e	// DPL=0，内核中断/异常
#define IDT_ATTR_USER		0xee	// DPL=3，用户可触发（如 int 0x80）

#define IDT_ENTRIES		256

struct idt_entry {
	uint16 offset_low;
	uint16 selector;
	uint8 zero;
	uint8 type_attr;
	uint16 offset_high;
} __attribute__((packed));

struct idt_ptr {
	uint16 limit;
	uint32 base;
} __attribute__((packed));

uint32 idt_table_addr(void);
uint16 idt_table_limit(void);
uint32 idt_get_handler(int vec);
uint16 idt_get_selector(int vec);
uint8 idt_get_type_attr(int vec);

/* GDT 段选择子（index << 3） */
#define SEG_KCODE		0x08	// 内核代码段，DPL=0
#define SEG_KDATA		0x10	// 内核数据段，DPL=0
#define SEG_UCODE		0x18	// 用户代码段，DPL=3
#define SEG_UDATA		0x20	// 用户数据段，DPL=3
#define SEG_TSS			0x28	// 32 位可用 TSS，DPL=0

/* GDT access 字节：P=1, S=1, Type/DPL 因段类型而异 */
#define GDT_KERNEL_CODE		0x9a	// 可执行、可读，DPL=0
#define GDT_KERNEL_DATA		0x92	// 可读写，DPL=0
#define GDT_USER_CODE		0xfa	// 可执行、可读，DPL=3
#define GDT_USER_DATA		0xf2	// 可读写，DPL=3
#define GDT_TSS			0x89	// 可用 32 位 TSS（系统段，P=1, DPL=0, Type=1001）

/* granularity：G=1, D/B=1, limit[19:16]=0xF → 4GB 平坦段 */
#define GDT_GRAN_4GB		0xcf

#define GDT_ACCESS_MASK		0xfe	/* 忽略 CPU 自动置位的 Accessed 位 */

#define GDT_ENTRIES		6

/* 32 位 TSS（104 字节）；硬件任务切换已弃用，esp0/ss0 供 ring3→ring0 用 */
struct tss {
	uint32 link;
	uint32 esp0;
	uint32 ss0;
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
	uint16 iomap_base;
} __attribute__((packed));

// GDT 描述符结构体
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

uint32 gdt_table_addr(void);
uint16 gdt_table_limit(void);
uint8 gdt_get_access(int idx);
uint8 gdt_get_granularity(int idx);

void tss_init(void);
void tss_set_esp0(uint32 esp);
uint32 tss_get_esp0(void);
uint32 tss_get_ss0(void);

struct trapframe {
	uint32 edi;	// esp+ 0  ← popal 最后弹出
	uint32 esi;	// esp+ 4
	uint32 ebp;	// esp+ 8
	uint32 oesp;	// esp+12  old esp, 压栈之前的 ESP（不是当前 ESP）
	uint32 ebx;	// esp+16
	uint32 edx;	// esp+20
	uint32 ecx;	// esp+24
	uint32 eax;	// esp+28  ← pushal 最先压入
	uint32 ds;	// esp+32 数据段选择子
	uint32 err;	// esp+36 错误码
	uint32 eip;	// esp+40 指令指针
	uint32 cs;	// esp+44 代码段选择子
	uint32 eflags;	// esp+48 标志寄存器
	uint32 esp;	// esp+52 栈顶指针，ring3 硬件压栈的用户 esp；内核态 trap 时无效
	uint32 ss;	// esp+56 栈段选择子，ring3 硬件压栈的用户 ss；内核态 trap 时无效
};

#endif