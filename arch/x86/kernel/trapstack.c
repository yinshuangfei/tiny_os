#include "types.h"
#include "param.h"
#include "mm/memlayout.h"

/* 每 CPU 调度栈；BSP entry.S 使用 kernel_stacks[0] */
char kernel_stacks[NR_CPUS][KSTACKSIZE] __attribute__((aligned(16)));
char interrupt_stacks[NR_CPUS][KSTACKSIZE] __attribute__((aligned(16)));

uint32 interrupt_stack_tops[NR_CPUS];

void trapstack_init(void)
{
	int i;

	for (i = 0; i < NR_CPUS; i++)
		interrupt_stack_tops[i] =
			(uint32)&interrupt_stacks[i][0] + KSTACKSIZE;
}
