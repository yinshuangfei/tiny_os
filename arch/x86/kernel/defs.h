#ifndef __DEFS_H__
#define __DEFS_H__

#include "types.h"
#include "utils.h"
#include "mm/vm.h"
#include "mm/slab.h"
#include "proc.h"
#include "lock/spinlock.h"

/** mm/ — 物理探测、buddy、slab、页表（对齐 Linux mm/） */
void *memset(void *dst, int c, uint n);
void *memcpy(void *dst, const void *src, uint n);
void pmm_init(void);
void mem_probe(void);
void *alloc_pages(unsigned int order);
void free_pages(void *addr, unsigned int order);
void *alloc_page(void);
void free_page(void *addr);
void get_page(void *addr);
void put_page(void *addr);
unsigned int page_refcount(void *addr);
unsigned int pmm_nr_free_pages(void);
unsigned int pmm_nr_pages(void);

void cpu_init(void);

/** fpu.c — 每进程 FXSAVE 与 lazy #NM */
void fpu_init(void);
void fpu_switch_away(void);
void fpu_drop(struct proc *p);
void fpu_clear(struct proc *p);
void fpu_fork(struct proc *child, struct proc *parent);
void fpu_nm(struct trapframe *tf);

/** printf.c / printk */
#include "printk.h"
#include <stdarg.h>
void printf(char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int vsprintf(char *buf, const char *fmt, va_list ap);
int snprintf(char *buf, uint size, const char *fmt, ...);
int vsnprintf(char *buf, uint size, const char *fmt, va_list ap);
void printfinit(void);
void panic(char *s);

/** serial.c */
void serial_init(void);
void uart_putc(char c);
int uart_getc(void);
void uart_puts(const char *s);
void uart_intr(void);

/** console.c — 系统控制台（printk / /dev/console） */
void console_init(void);
void consputc(int c);
void console_write(const char *s, unsigned int n);
void console_intr(int c);
int console_getc(void);
int console_ioctl(unsigned int req, unsigned int uarg);
int console_is_canon(void);
void console_set_fg(int pid);
int console_get_fg(void);

/** interrupt.c — 8259 / APIC */
void pic_eoi(int irq);
void pic_init(void);
void irq_eoi(int irq);
int apic_init(void);

/** kbd.c — PS/2 键盘 */
void kbd_init(void);
void kbd_intr(void);

/** vga.c */
void vga_init(void);
void vga_putc(char c);
void vga_putc_at(int row, int col, char c);

#include "proc.h"
#include "lock/spinlock.h"

/** interrupt.c */
#include "interrupt.h"

/** timer.c */
#include "timer.h"

/** syscall */
#include "syscall.h"

/** fs */
void fs_init(void);

/** block/ — Linux 风格块设备层 */
void blk_init(void);

/** ide.c — ATA PIO（多控制器扫描；drive 为逻辑盘号） */
void ide_init(void);
int ide_ndisks(void);
int ide_read(int drive, uint lba, void *buf);
int ide_write(int drive, uint lba, const void *buf);
uint ide_nsectors(int drive);

#endif
