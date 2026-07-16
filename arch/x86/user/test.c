#include "user.h"

static int test_check(const char *name, int ok)
{
	printf("  syscall %s(): %s\n", name, ok ? "OK" : "FAIL");
	return ok;
}

void getpid_test(void)
{
	int pid = getpid();

	printf("get pid=%d\n", pid);
	test_check("getpid", pid > 0 && pid < 0xff);
}

void write_test(void)
{
	write(1, "hello, world\n", 13);
	test_check("write", 1);
}

void nanosleep_test(void)
{
	int rc;
	struct timespec ts = { 1, 0 };	/* 1s，100Hz → 100 tick */
	rc = nanosleep(&ts, 0);
	printf("nanosleep rc=%d\n", rc);
	test_check("nanosleep", 1);
}

/**
 * 可以只使用
 * int main(int argc, char *argv[])
*/
int main(int argc, char *argv[], char *envp[])
{
	int i;

	for (i = 0; i < argc; i++)
		printf("argv[%d] = %s\n", i, argv[i]);

	if (envp) {
		for (i = 0; envp[i]; i++)
			printf("envp[%d] = %s\n", i, envp[i]);
	}

	write_test();
	getpid_test();
	nanosleep_test();
	exit(0);
}