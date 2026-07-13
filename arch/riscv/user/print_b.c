#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "lib/user.h"
#include "kernel/fcntl.h"

int main(int argc, char *argv[])
{
	for (;;) {
		printf("B");
		sleep(1);
	}
	return 0;
}
