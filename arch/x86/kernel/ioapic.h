/*
 * I/O APIC：把 ISA IRQ（= GSI，QEMU 恒等）路由到 IDT 向量 0x20+irq。
 */
#ifndef __IOAPIC_H__
#define __IOAPIC_H__

#include "types.h"

#define IOAPIC_DEFAULT_BASE	0xFEC00000u	/* IOAPIC 默认基址 */

void ioapic_map(void);
int ioapic_init(void);
void ioapic_enable(int irq, int cpunum);

#endif
