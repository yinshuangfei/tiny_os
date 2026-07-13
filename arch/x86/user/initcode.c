/*
 * 第一个用户态 C 程序：演示 write / getpid / nanosleep / exit 系统调用。
 * 链接地址须与 memlayout.h 中 USERBASE 一致。
 */
#include "user.h"

int main(void)
{
	printf("hello world\n");
	exit(0);
}
