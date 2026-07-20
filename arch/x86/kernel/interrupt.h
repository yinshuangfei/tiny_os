#ifndef __INTERRUPT_H__
#define __INTERRUPT_H__

#define IRQ_0_TIMER		0
#define IRQ_1_KEYBOARD		1
#define IRQ_2_CASCADE		2	/* 主片级联从片 */
#define IRQ_4_COM1		4
#define IRQ_10_IDE1B		10	/* isa-ide 第四通道（Makefile irq=10） */
#define IRQ_11_IDE1A		11	/* isa-ide 第三通道（Makefile irq=11） */
#define IRQ_14_IDE0		14	/* 主 IDE 通道 */
#define IRQ_15_IDE1		15	/* 次 IDE 通道 */

void pic_eoi(int irq);
void pic_init(void);

#endif