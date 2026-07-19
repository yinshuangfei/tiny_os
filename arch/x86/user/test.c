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

/*
 * lseek：对 /hello 测 SEEK_SET / SEEK_CUR / SEEK_END，并确认管道不可定位。
 * 文件内容：Hello from Tiny-OS ramfs!\n
 */
void lseek_test(void)
{
	int fd, n, off, ok;
	char buf[8];
	int p[2];

	ok = 1;
	fd = open("/hello", O_RDONLY);
	if (fd < 0) {
		printf("lseek: open /hello failed\n");
		test_check("lseek", 0);
		return;
	}

	/* SEEK_SET → 偏移 6，读 "from" */
	off = lseek(fd, 6, SEEK_SET);
	if (off != 6) {
		printf("lseek SEEK_SET: off=%d expect 6\n", off);
		ok = 0;
	}
	n = read(fd, buf, 4);
	if (n != 4 || buf[0] != 'f' || buf[1] != 'r' ||
	    buf[2] != 'o' || buf[3] != 'm') {
		printf("lseek SEEK_SET read failed n=%d\n", n);
		ok = 0;
	}

	/* SEEK_CUR：当前位置 10，回退 4 → 6 */
	off = lseek(fd, -4, SEEK_CUR);
	if (off != 6) {
		printf("lseek SEEK_CUR: off=%d expect 6\n", off);
		ok = 0;
	}

	/* SEEK_END：到末尾再退 1，应读到 '\n' */
	off = lseek(fd, 0, SEEK_END);
	if (off <= 0) {
		printf("lseek SEEK_END: off=%d\n", off);
		ok = 0;
	}
	off = lseek(fd, -1, SEEK_END);
	if (off < 0) {
		printf("lseek SEEK_END-1 failed\n");
		ok = 0;
	}
	n = read(fd, buf, 1);
	if (n != 1 || buf[0] != '\n') {
		printf("lseek SEEK_END read failed n=%d c=%d\n",
		       n, n == 1 ? (int)buf[0] : -1);
		ok = 0;
	}

	/* 负偏移非法 */
	if (lseek(fd, -1, SEEK_SET) != -1) {
		printf("lseek negative SEEK_SET should fail\n");
		ok = 0;
	}
	close(fd);

	/* 管道不可 lseek（类 ESPIPE） */
	if (pipe(p) < 0) {
		printf("lseek: pipe failed\n");
		ok = 0;
	} else {
		if (lseek(p[0], 0, SEEK_SET) != -1 ||
		    lseek(p[1], 0, SEEK_SET) != -1) {
			printf("lseek on pipe should fail\n");
			ok = 0;
		}
		close(p[0]);
		close(p[1]);
	}

	test_check("lseek", ok);
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
	lseek_test();
	exit(0);
}
