/*
 * Local APIC（xAPIC MMIO，教学单核）。
 * 默认基址 0xFEE00000；EOI 写 EOI 寄存器。
 */
#ifndef __LAPIC_H__
#define __LAPIC_H__

#include "types.h"

#define LAPIC_DEFAULT_BASE	0xFEE00000u	/* Local APIC 默认基址 */

/* IA32_APIC_BASE MSR */
#define MSR_IA32_APIC_BASE	0x1B		/* IA32_APIC_BASE MSR 寄存器地址 */
#define APIC_BASE_ENABLE	(1ull << 11)	/* APIC 使能 */
#define APIC_BASE_BSP		(1ull << 8)	/* BSP 使能 */

void lapic_map(void);
int lapic_init(void);
void lapic_timer_init(unsigned int hz);
void lapic_eoi(void);
uint32 lapic_id(void);
int lapic_present(void);

#endif
