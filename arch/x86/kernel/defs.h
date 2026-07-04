#ifndef __DEFS_H__
#define __DEFS_H__

#include "types.h"

/** alloc.c — Linux-style buddy page allocator */
void *memset(void *dst, int c, uint n);
void pmm_init(void);
void *alloc_pages(unsigned int order);
void free_pages(void *addr, unsigned int order);
void *alloc_page(void);
void free_page(void *addr);
unsigned int pmm_nr_free_pages(void);

/** interrupt.c */
void idt_set_gate(int vec, void (*handler)(void), uint16 selector, uint8 type_attr);
void idt_init(void);
void gdt_init(void);

/** printf.c */
void printf(char *fmt, ...);
void panic(char *s);

/** serial.c */
void serial_init(void);
void uart_putc(char c);
void uart_puts(const char *s);

/** timer.c */
void pic_init(void);
void pit_init(void);
unsigned int timer_ticks(void);

/** vm.c */
void kvm_init(void);
extern pde_t *kernel_pgdir;

#endif
