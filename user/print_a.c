#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "lib/user.h"
#include "kernel/fcntl.h"

int main(void)
{
	for (;;) {
		printf("A");
	}
	return 0;
}
