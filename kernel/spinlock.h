#ifndef __spinlock_h__
#define __spinlock_h__

#include "types.h"

// Mutual exclusion lock.
struct spinlock {
	uint locked;       // Is the lock held?

	// For debugging:
	char *name;        // Name of lock.
	struct cpu *cpu;   // The cpu holding the lock.
};

#endif /** __spinlock_h__ */