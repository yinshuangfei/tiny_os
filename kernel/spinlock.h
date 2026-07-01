#ifndef __spinlock_h__
#define __spinlock_h__

#include "types.h"

// Mutual exclusion lock.
struct spinlock {
	// 该值为原子操作值，值为1，表示锁定
	uint locked;       // Is the lock held?

	// For debugging:
	char *name;        // Name of lock.
	struct cpu *cpu;   // The cpu holding the lock.
};

#endif /** __spinlock_h__ */