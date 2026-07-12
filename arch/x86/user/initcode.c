/*
 * 第一个用户态 C 程序：演示 write / getpid / nanosleep / exit 系统调用。
 * 链接地址须与 memlayout.h 中 USERBASE 一致。
 */
#include "user.h"

int main(void)
{
	static const char msg[] = "hello from user\n";
	struct timespec ts = { 1, 0 };	/* 1s，100Hz → 100 tick */

	write(1, msg, sizeof(msg) - 1);
	getpid();
	nanosleep(&ts, 0);
	write(1, "woke up\n", 8);
	exit(0);
}
