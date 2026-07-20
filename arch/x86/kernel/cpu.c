/*
 * CPU 特性探测与使能（CPUID / CR0 / CR4）
 */
#include "types.h"
#include "defs.h"
#include "x86.h"

/**
 * 开启 SSE：
 * GCC 对 char total_size[24] = {0} 常会生成 SSE 指令（movdqa / movups）来清零栈
 * 数组，而不是简单的 mov 循环。tiny OS 在 pmm_init 阶段还没有在 CR0/CR4 里开启 SSE。
 * 此时执行这些指令可能触发 #UD（非法指令） 异常。
*/

static int cpu_has_sse(void)
{
	uint32 eax, ebx, ecx, edx;

	if (!cpu_has_cpuid())
		return 0;

	cpuid(0, &eax, &ebx, &ecx, &edx);
	if (eax < 1)
		return 0;

	cpuid(1, &eax, &ebx, &ecx, &edx);
	return (edx >> 25) & 1;
}

void cpu_init(void)
{
	uint cr0, cr4;

	if (!cpu_has_sse()) {
		printf("cpu: SSE not supported\n");
		return;
	}

	cr0 = r_cr0();
	cr0 |= CR0_MP;			/* TS=1 时 WAIT/#NM 行为完整 */
	cr0 |= CR0_NE;			/* 原生 FPU 错误报告 */
	cr0 &= ~CR0_EM;			/* 不用软件模拟 */
	cr0 &= ~CR0_TS;
	w_cr0(cr0);

	cr4 = r_cr4();
	cr4 |= CR4_OSFXSR | CR4_OSXMMEXCPT;
	w_cr4(cr4);

	fpu_init();
	printk(KERN_INFO "cpu: FPU/SSE enabled\n");
}
