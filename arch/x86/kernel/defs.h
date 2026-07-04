#ifndef __DEFS_H__
#define __DEFS_H__

#include "types.h"

/** alloc.c */
void *memset(void *dst, int c, uint n);
void alloc_pt_reset(void);
pagetable_t alloc_pt(void);

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
