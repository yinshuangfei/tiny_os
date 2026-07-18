#ifndef __INTERRUPT_H__
#define __INTERRUPT_H__

#define IRQ_0_TIMER		0
#define IRQ_1_KEYBOARD		1
#define IRQ_4_COM1		4

void pic_eoi(int irq);
void pic_init(void);

#endif