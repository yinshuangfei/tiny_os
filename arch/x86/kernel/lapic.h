/*
 * Local APIC（xAPIC MMIO）。
 * BSP/AP 共用：EOI、周期定时器、INIT-SIPI 启动 AP。
 */
#ifndef __LAPIC_H__
#define __LAPIC_H__

#include "types.h"

#define LAPIC_DEFAULT_BASE	0xFEE00000u

#define MSR_IA32_APIC_BASE	0x1B
#define APIC_BASE_ENABLE	(1ull << 11)
#define APIC_BASE_BSP		(1ull << 8)

/* 供 trap.S 按 APIC ID 取 per-CPU 中断栈（未映射前为 0） */
extern volatile uint32 *lapic;

void lapic_map(void);
int lapic_init(void);
void lapic_timer_init(unsigned int hz);
void lapic_timer_init_ap(void);
void lapic_eoi(void);
uint32 lapic_id(void);
int lapic_present(void);
void lapic_startap(uint32 apicid, uint32 addr);
void lapic_microdelay(unsigned int us);

#endif
