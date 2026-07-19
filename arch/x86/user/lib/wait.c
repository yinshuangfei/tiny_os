/*
 * wait(3)：等待任意子进程退出（封装 waitpid(-1, status, 0)）。
 */
#include "user.h"

int wait(int *status)
{
	return waitpid(-1, status, 0);
}
