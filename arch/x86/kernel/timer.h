#ifndef __TIMER_H__
#define __TIMER_H__

#include "trap.h"

#define TIMER_HZ  100

void pit_init(void);
void timer_handler(void);
void timer_trap_user(struct trapframe *tf);
unsigned int timer_ticks(void);

#endif
