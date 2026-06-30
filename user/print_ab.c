#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"
#include "kernel/fcntl.h"

char *argv[] = { "test", 0 };

int main(void)
{
	int pid;

	pid = fork();
	if (pid < 0) {
		printf("init: fork failed\n");
		exit(1);
	}
	if (pid == 0) {
		exec("print_a", argv);
		printf("init: exec failed\n");
		exit(1);
	}

	exec("print_b", argv);
	printf("init: exec failed\n");
	exit(1);
}
