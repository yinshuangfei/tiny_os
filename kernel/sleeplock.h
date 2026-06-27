#ifndef __sleeplock_h__
#define __sleeplock_h__

#include "types.h"
#include "spinlock.h"

// Long-term locks for processes
struct sleeplock {
	uint locked;       // Is the lock held?
	struct spinlock lk; // spinlock protecting this sleep lock

	// For debugging:
	char *name;        // Name of lock.
	int pid;           // Process holding lock
};

#endif /** __sleeplock_h__ */