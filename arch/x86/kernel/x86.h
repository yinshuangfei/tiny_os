#ifndef __X86_H__
#define __X86_H__

#include "types.h"

struct gdt_ptr;
struct idt_ptr;

/** set interrupt flag */
static inline void sti(void)
{
	__asm__ volatile ("sti");
}

/** clear interrupt flag */
static inline void cli(void)
{
	__asm__ volatile ("cli");
}

/** read EFLAGS.IF (1 = interrupts enabled) */
static inline int intr_get(void)
{
	uint32 eflags;

	__asm__ volatile ("pushfl; popl %0" : "=r"(eflags));
	return (eflags & 0x200) != 0;
}

static inline void intr_off(void)
{
	cli();
}

static inline void intr_on(void)
{
	sti();
}

/** halt */
static inline void halt(void)
{
	__asm__ volatile ("hlt");
}

/** inb */
static inline unsigned char inb(unsigned short port)
{
	unsigned char val;

	__asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}

/** outb */
static inline void outb(unsigned short port, unsigned char val)
{
	__asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/** inw */
static inline unsigned short inw(unsigned short port)
{
	unsigned short val;

	__asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}

/** outw */
static inline void outw(unsigned short val, unsigned short port)
{
	__asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

/** 读取当前指令指针（EIP 不可直接 mov，用 call/pop 取返回地址） */
static inline uint r_pc(void)
{
	uint val;

	__asm__ volatile (
		"call 1f\n"
		"1:  pop %0" : "=r"(val)
	);
	return val;
}

/** 读取段寄存器当前选择子 */
static inline uint16 r_cs(void)
{
	uint16 val;

	__asm__ volatile ("movw %%cs, %0" : "=r"(val));
	return val;
}

static inline uint16 r_ds(void)
{
	uint16 val;

	__asm__ volatile ("movw %%ds, %0" : "=r"(val));
	return val;
}

static inline uint16 r_es(void)
{
	uint16 val;

	__asm__ volatile ("movw %%es, %0" : "=r"(val));
	return val;
}

static inline uint16 r_ss(void)
{
	uint16 val;

	__asm__ volatile ("movw %%ss, %0" : "=r"(val));
	return val;
}

/** read task register */
static inline uint16 r_tr(void)
{
	uint16 val;

	__asm__ volatile ("str %0" : "=r"(val));
	return val;
}

/** read GDT pointer */
static inline void r_gdtr(struct gdt_ptr *ptr)
{
	__asm__ volatile ("sgdt %0" : "=m"(*ptr));
}

/** read IDT pointer */
static inline void r_idtr(struct idt_ptr *ptr)
{
	__asm__ volatile ("sidt %0" : "=m"(*ptr));
}

static inline void cpuid(uint32 leaf, uint32 *eax, uint32 *ebx,
			 uint32 *ecx, uint32 *edx)
{
	__asm__ volatile ("cpuid"
			  : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
			  : "a"(leaf), "c"(0));
}

static inline int cpu_has_cpuid(void)
{
	uint32 a, b;

	__asm__ volatile (
		"pushfl\n"
		"popl %0\n"
		"movl %0, %1\n"
		"xorl $0x200000, %0\n"
		"pushl %0\n"
		"popfl\n"
		"pushfl\n"
		"popl %0\n"
		"pushl %1\n"
		"popfl\n"
		: "=r"(a), "=r"(b)
		:
		: "memory"
	);
	return a != b;
}

/** CR0 flags */
#define CR0_EM  0x00000004	/* emulation (FP/SSE trap if set) */
#define CR0_TS  0x00000008	/* task switched (FP/SSE trap if set) */
#define CR0_PG  0x80000000	/* paging */

/** CR4 flags */
#define CR4_OSFXSR     0x00000200	/* enable SSE instructions */
#define CR4_OSXMMEXCPT 0x00000400	/* enable #XM for SSE */

static inline uint r_cr0(void)
{
	uint val;

	__asm__ volatile ("movl %%cr0, %0" : "=r"(val));
	return val;
}

static inline void w_cr0(uint val)
{
	__asm__ volatile ("movl %0, %%cr0" : : "r"(val));
}

static inline uint r_cr3(void)
{
	uint val;

	__asm__ volatile ("movl %%cr3, %0" : "=r"(val));
	return val;
}

static inline void w_cr3(uint val)
{
	__asm__ volatile ("movl %0, %%cr3" : : "r"(val));
}

static inline uint r_cr4(void)
{
	uint val;

	__asm__ volatile ("movl %%cr4, %0" : "=r"(val));
	return val;
}

static inline void w_cr4(uint val)
{
	__asm__ volatile ("movl %0, %%cr4" : : "r"(val));
}

static inline uint r_esp(void)
{
	uint esp;

	__asm__ volatile("movl %%esp, %0" : "=r"(esp));
	return esp;
}

#endif