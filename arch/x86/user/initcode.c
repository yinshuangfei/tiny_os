/*
 * 第一个用户态 C 程序：演示 write / getpid / exit 系统调用。
 * 链接地址须与 memlayout.h 中 USERBASE 一致。
 */
#include "user.h"

int main(void)
{
	static const char msg[] = "hello from user\n";

	write(1, msg, sizeof(msg) - 1);
	getpid();
	exit(0);
}
