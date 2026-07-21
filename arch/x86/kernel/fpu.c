/*
 * 每进程 FPU/SSE 状态（FXSAVE 512 字节）与 lazy 切换。
 *
 * 调度切走时：若本核 owner 是当前进程则先 fxsave，再置 CR0.TS；
 * 下次 FP/SSE 指令触发 #NM，再恢复当前进程状态。
 * SMP：每核独立 FPU，故 fpu_owner 为 per-CPU。
 */
#include "types.h"
#include "defs.h"
#include "param.h"
#include "proc.h"
#include "mp.h"
#include "x86.h"
#include "trap.h"

#define MXCSR_DEFAULT	0x1f80u	/* 屏蔽全部 SSE 异常（Intel 上电默认） */

/* 各核当前占用 FPU/SSE 硬件的进程；NULL 表示可丢弃 */
static struct proc *fpu_owner[NR_CPUS];

/* 清空 TS 标志 */
static inline void fpu_clts(void)
{
	w_cr0(r_cr0() & ~CR0_TS);
}

/* 保存 FPU/SSE 状态 */
static void fxsave(void *mem)
{
	if (((uint)mem) & 0xf)
		panic("fxsave: unaligned");
	__asm__ volatile("fxsave (%0)" : : "r"(mem) : "memory");
}

/* 恢复 FPU/SSE 状态 */
static void fxrstor(void *mem)
{
	if (((uint)mem) & 0xf)
		panic("fxrstor: unaligned");
	__asm__ volatile("fxrstor (%0)" : : "r"(mem) : "memory");
}

static void fpu_hw_init(void)
{
	uint32 mxcsr = MXCSR_DEFAULT;

	__asm__ volatile("fninit");
	__asm__ volatile("ldmxcsr %0" : : "m"(mxcsr) : "memory");
}

/* cpu_init 在开启 OSFXSR 后调用：内核启动时干净 x87/MXCSR */
void fpu_init(void)
{
	int i;

	fpu_hw_init();
	for (i = 0; i < NR_CPUS; i++)
		fpu_owner[i] = 0;
}

/* 进程即将被切走：写回本核 dirty FPU，置 TS */
void fpu_switch_away(void)
{
	struct proc *p = myproc();
	int id = cpu_id();

	if (p && fpu_owner[id] == p) {
		fpu_clts();
		fxsave(p->fpu_state);
		p->fpu_used = 1;
		fpu_owner[id] = 0;
	}
	w_cr0(r_cr0() | CR0_TS);
}

/* 清空 FPU/SSE 状态 */
void fpu_drop(struct proc *p)
{
	int i;

	if (!p)
		return;
	for (i = 0; i < NR_CPUS; i++) {
		if (fpu_owner[i] == p)
			fpu_owner[i] = 0;
	}
}

/* exec 后丢弃旧映像的浮点状态 */
void fpu_clear(struct proc *p)
{
	if (!p)
		return;
	p->fpu_used = 0;
	fpu_drop(p);
}

/* fork：子进程继承父进程已保存的 FPU 映像 */
void fpu_fork(struct proc *child, struct proc *parent)
{
	int id;

	if (!child || !parent) {
		if (child)
			child->fpu_used = 0;
		return;
	}
	if (!parent->fpu_used && fpu_owner[cpu_id()] != parent) {
		child->fpu_used = 0;
		return;
	}
	/* 父仍占用本核硬件时先写回内存，再拷贝 */
	id = cpu_id();
	if (fpu_owner[id] == parent) {
		fpu_clts();
		fxsave(parent->fpu_state);
		parent->fpu_used = 1;
	}
	if (!parent->fpu_used) {
		child->fpu_used = 0;
		return;
	}
	memcpy(child->fpu_state, parent->fpu_state, FX_SIZE);
	child->fpu_used = 1;
}

/*
 * #NM：懒切换 FPU/SSE 上下文，然后返回重试故障指令。
 */
void fpu_nm(struct trapframe *tf)
{
	struct proc *p = myproc();
	int id = cpu_id();

	(void)tf;
	fpu_clts();

	if (!p)
		return;

	if (fpu_owner[id] == p)
		return;

	if (fpu_owner[id]) {
		fxsave(fpu_owner[id]->fpu_state);
		fpu_owner[id]->fpu_used = 1;
	}

	if (p->fpu_used)
		fxrstor(p->fpu_state);
	else {
		fpu_hw_init();
		p->fpu_used = 1;
	}
	fpu_owner[id] = p;
}
