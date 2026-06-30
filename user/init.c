// init: The initial user-level program

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "lib/user.h"
#include "kernel/fcntl.h"

char *argv[] = { "sh", 0 };

int main(void)
{
	int pid, wpid;

	if (open("console", O_RDWR) < 0) {
		mknod("console", CONSOLE, 0);
		/** fd 0, stdin */
		open("console", O_RDWR);
	}

	/** fd 1, stdout */
	dup(0);
	/** fd 2, stderr */
	dup(0);
	/** 后续的所有程序都继承这些文件描述符 */

	for (;;) {
		printf("init: starting sh\n");
		pid = fork();
		if (pid < 0) {
			printf("init: fork failed\n");
			exit(1);
		}
		if (pid == 0) {
			exec("sh", argv);
			printf("init: exec sh failed\n");
			exit(1);
		}

		// parent process
		for (;;) {
			// this call to wait() returns if the shell exits,
			// or if a parentless process exits.
			wpid = wait((int *) 0);
			if (wpid == pid) {
				// the shell exited; restart it.
				break;
			} else if (wpid < 0) {
				printf("init: wait returned an error\n");
				exit(1);
			} else {
				// it was a parentless process; do nothing.
			}
		}
	}
}
