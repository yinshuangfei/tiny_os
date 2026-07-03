#ifndef __X86_H__
#define __X86_H__

#include "types.h"

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