#include "types.h"
#include "defs.h"

uint64 sys_exit(void)
{
	int n;
	if (argint(0, &n) < 0)
		return -1;
	exit(n);
	return 0;  // not reached
}