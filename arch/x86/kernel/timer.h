#ifndef __TIMER_H__
#define __TIMER_H__

#include "interrupt.h"

#define TIMER_HZ  100

void pic_init(void);
void pit_init(void);
void pic_eoi(int irq);
void timer_handler(void);
void timer_trap_user(struct trapframe *tf);
unsigned int timer_ticks(void);

#endif
