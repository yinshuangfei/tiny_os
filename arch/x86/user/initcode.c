/*
 * 第一个用户态 C 程序：演示 write / getpid / nanosleep / exit 系统调用。
 * 链接地址须与 memlayout.h 中 USERBASE 一致。
 */
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

int main(void)
{
	write_test();
	getpid_test();
	nanosleep_test();
	exit(0);
}
