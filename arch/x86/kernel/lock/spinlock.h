#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

#include "../types.h"

struct cpu;

struct spinlock {
	uint locked;
	char *name;
	struct cpu *cpu;
};

void initlock(struct spinlock *lk, char *name);
void acquire(struct spinlock *lk);
void release(struct spinlock *lk);
int holding(struct spinlock *lk);
void push_off(void);
void pop_off(void);

#endif
