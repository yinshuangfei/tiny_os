#ifndef __X86_H__
#define __X86_H__

#include "types.h"

struct gdt_ptr;

struct idt_ptr;

static inline void sti(void)
{
	__asm__ volatile ("sti");
}

/** clear interrupt flag */
static inline void cli(void)
{
	__asm__ volatile ("cli");
}

/** halt */
static inline void halt(void)
{
	__asm__ volatile ("hlt");
}

/** outb */
static inline void outb(unsigned short port, unsigned char val)
{
	__asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
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

static inline void r_gdtr(struct gdt_ptr *ptr)
{
	__asm__ volatile ("sgdt %0" : "=m"(*ptr));
}

static inline void r_idtr(struct idt_ptr *ptr)
{
	__asm__ volatile ("sidt %0" : "=m"(*ptr));
}

/** CR0 flags */
#define CR0_PG  0x80000000	/* paging */

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

#endif