#include "defs.h"
#include "x86.h"
#include "timer.h"
#include "proc.h"

/** PIT: Programmable Interval Timer (可编程间隔定时器) */
/** 产生定时器中断 IRQ0 (Timer Tick) */
#define PIT_CMD   0x43		// 命令端口
#define PIT_CH0   0x40		// 通道 0 端口

#define PIT_FREQ  1193182	/* 8254 输入时钟 1.193182 MHz */

static volatile unsigned int ticks;

/** 配置 8254 可编程间隔定时器 */
void pit_init(void)
{
	uint32 divisor = PIT_FREQ / TIMER_HZ;

	if (divisor > 0xffff)
		panic("pit: divisor overflow");

	/**
	 * bit 7–6: 通道选择：00 通道 0，01 通道 1，10 通道 2，11 读回波特率
	 * bit 5–4: 存取模式：00 方式 0，01 方式 1，10 方式 2，11 方式 3 (方波发生器)
	 * bit 3–0: 存取频率：二进制计数，BCD 计数
	 * 0x36 = 0011 0110
	 * 00: 通道 0
	 * 11: 方式 3
	 * 0110: 二进制计数（非 BCD）
	 */
	outb(PIT_CMD, 0x36);
	outb(PIT_CH0, divisor & 0xff);
	outb(PIT_CH0, (divisor >> 8) & 0xff);

	printk(KERN_INFO "pit: system pit(8254) initialized, timer frequency: %d Hz\n", TIMER_HZ);
}

void timer_handler(void)
{
	/* IRQ0：先 EOI 再干活，避免 8259 边沿模式下 handler 过长丢中断 */
	pic_eoi(IRQ_0_TIMER);
	ticks++;
	sched_tick();
}

/*
 * 定时器从 ring3 陷入时调用：记录 kframe 并执行 tick。
 * 返回后汇编路径可安全调用 preempt_check → yield。
 */
void timer_trap_user(struct trapframe *tf)
{
	struct proc *p = myproc();

	if (p)
		p->kframe = tf;
	timer_handler();
}

unsigned int timer_ticks(void)
{
	return ticks;
}
