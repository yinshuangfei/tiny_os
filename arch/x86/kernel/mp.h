/*
 * 多核（SMP）教学子集：BSP 经 INIT-SIPI-SIPI 启动 AP，
 * 共享全局就绪队列，每核跑 scheduler() + 本核 LAPIC timer。
 * 对齐 Linux smpboot：trampoline → start_secondary → scheduler。
 *
 * 数量模型（对齐 Linux）：
 *   NR_CPUS            — NR_CPUS，静态数组上界（param.h / make NR_CPUS=）
 *   SETUP_MAX_CPUS  — setup_max_cpus，启动时最多拉起几个（make CPUS=）
 *   num_online_cpus — 实际 online 数（bring-up 后写入）
 */
#ifndef __MP_H__
#define __MP_H__

#include "types.h"
#include "param.h"

/*
 * SETUP_MAX_CPUS ≈ Linux setup_max_cpus（boot 参数 maxcpus=）。
 * Makefile：-DSETUP_MAX_CPUS=$(CPUS)，这里只表示“最多拉起多少个 CPU”。
 * 实际 present CPU 数由运行时探测（如 QEMU fw_cfg）决定。
 */
#ifndef SETUP_MAX_CPUS
#define SETUP_MAX_CPUS	NR_CPUS
#endif

#if SETUP_MAX_CPUS < 1
#error SETUP_MAX_CPUS must be >= 1
#endif
#if SETUP_MAX_CPUS > NR_CPUS
#error SETUP_MAX_CPUS exceeds NR_CPUS; raise NR_CPUS (make NR_CPUS=... CPUS=...)
#endif

extern int ncpu;			/* online CPU 数（num_online_cpus） */
extern volatile int ap_started[NR_CPUS];

static inline int num_online_cpus(void)
{
	return ncpu;
}

int cpu_id(void);			/* 逻辑 CPU 下标 0 .. ncpu-1 */
void mp_init(void);			/* BSP：按运行时 CPU 数且不超过 SETUP_MAX_CPUS 启动 AP */
void start_secondary(void) __attribute__((noreturn));	/* AP 入口 */

#endif
