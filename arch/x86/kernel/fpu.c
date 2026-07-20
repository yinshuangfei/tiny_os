/*
 * 每进程 FPU/SSE 状态（FXSAVE 512 字节）与 lazy 切换。
 *
 * 调度切走时置 CR0.TS；下次 FP/SSE 指令触发 #NM，再保存旧 owner、恢复当前。
 */
#include "types.h"
#include "defs.h"
#include "proc.h"
#include "x86.h"
#include "trap.h"

#define MXCSR_DEFAULT	0x1f80u	/* 屏蔽全部 SSE 异常（Intel 上电默认） */

/* 当前占用 FPU/SSE 硬件的进程；NULL 表示硬件状态可丢弃 */
static struct proc *fpu_owner;

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
	fpu_hw_init();
	fpu_owner = 0;
}

/* 进程即将被切走：置 TS，下次 FP/SSE 再懒切换 */
void fpu_switch_away(void)
{
	w_cr0(r_cr0() | CR0_TS);
}

/* 清空 FPU/SSE 状态 */
void fpu_drop(struct proc *p)
{
	if (p && fpu_owner == p)
		fpu_owner = 0;
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
	if (!child || !parent) {
		if (child)
			child->fpu_used = 0;
		return;
	}
	if (!parent->fpu_used) {
		child->fpu_used = 0;
		return;
	}
	/* 父仍占用硬件时先写回内存，再拷贝 */
	if (fpu_owner == parent) {
		fpu_clts();
		fxsave(parent->fpu_state);
		/* 父仍是 owner；保持 TS 清，或由后续 switch_away 再置 */
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

	(void)tf;
	fpu_clts();

	if (!p)
		return;

	if (fpu_owner == p)
		return;

	if (fpu_owner) {
		fxsave(fpu_owner->fpu_state);
		fpu_owner->fpu_used = 1;
	}

	if (p->fpu_used)
		fxrstor(p->fpu_state);
	else {
		fpu_hw_init();
		p->fpu_used = 1;
	}
	fpu_owner = p;
}
