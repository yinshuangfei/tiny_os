#include "user.h"
#include "signal.h"

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

static volatile int sig_got;

static void on_usr1(int sig)
{
	sig_got = sig;
	printf("handler: got signal %d\n", sig);
}

/* 注册 SIGUSR1 handler，向自身 kill，验证 handler 与 sigreturn */
void signal_test(void)
{
	sighandler_t old;

	old = signal(SIGUSR1, on_usr1);
	(void)old;

	sig_got = 0;
	if (kill(getpid(), SIGUSR1) < 0) {
		printf("kill self failed\n");
		test_check("signal", 0);
		return;
	}
	if (sig_got != SIGUSR1) {
		printf("FAIL: got=%d expect %d\n", sig_got, SIGUSR1);
		test_check("signal", 0);
		return;
	}
	printf("signal OK\n");
	test_check("signal", 1);
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
	signal_test();
	exit(0);
}
