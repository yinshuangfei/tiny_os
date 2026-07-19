/*
 * sbrk(3)：相对调整程序断点（基于 brk 系统调用）。
 */
#include "user.h"

void *sbrk(int incr)
{
	char *old;
	char *neu;

	/* brk(0) 非法 → 内核返回当前断点（同 Linux 失败返回旧值） */
	old = (char *)brk((void *)0);
	if (incr == 0)
		return old;

	neu = (char *)brk(old + incr);
	if (neu == old)
		return (void *)-1;
	return old;
}
