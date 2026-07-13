/*
 * sleep(3) / usleep(3)：经 nanosleep 系统调用实现（与常见 libc 一致）。
 */
#include "user.h"

unsigned int sleep(unsigned int seconds)
{
	struct timespec req, rem;

	if (seconds == 0)
		return 0;

	req.tv_sec = seconds;
	req.tv_nsec = 0;
	rem.tv_sec = 0;
	rem.tv_nsec = 0;

	if (nanosleep(&req, &rem) == 0)
		return 0;

	/* 被提前唤醒时返回剩余秒数（本内核暂不填充 rem，则返回原值） */
	return rem.tv_sec ? rem.tv_sec : seconds;
}

int usleep(unsigned int usec)
{
	struct timespec req;

	req.tv_sec = usec / 1000000u;
	req.tv_nsec = (usec % 1000000u) * 1000u;
	return nanosleep(&req, 0);
}
