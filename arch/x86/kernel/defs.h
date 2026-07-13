#ifndef __DEFS_H__
#define __DEFS_H__

#include "types.h"
#include "utils.h"
#include "vm.h"
#include "slab.h"
#include "proc.h"
#include "spinlock.h"

/** buddy.c — Linux-style buddy page allocator */
void *memset(void *dst, int c, uint n);
void pmm_init(void);
void mem_probe(void);
void *alloc_pages(unsigned int order);
void free_pages(void *addr, unsigned int order);
void *alloc_page(void);
void free_page(void *addr);
unsigned int pmm_nr_free_pages(void);

/** cpu.c */
void cpu_init(void);


/** printf.c */
void printf(char *fmt, ...);
void printfinit(void);
void panic(char *s);

/** serial.c */
void serial_init(void);
void uart_putc(char c);
int uart_getc(void);
void uart_puts(const char *s);

#include "proc.h"
#include "spinlock.h"

/** timer.c */
#include "timer.h"

/** syscall */
#include "syscall.h"

#endif
