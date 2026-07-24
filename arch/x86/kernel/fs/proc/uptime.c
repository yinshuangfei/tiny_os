/*
 * /proc/uptime（对齐 Linux：秒.百分秒 + idle，教学版 idle 暂与 uptime 相同）。
 */
#include "../../types.h"
#include "../../defs.h"
#include "../../timer.h"
#include "internal.h"

int uptime_show(char *buf, uint size)
{
	unsigned int ticks, sec, centi;

	ticks = timer_ticks();
	sec = ticks / TIMER_HZ;
	centi = (ticks % TIMER_HZ) * 100 / TIMER_HZ;
	/* Linux：uptime idle；无 per-CPU idle 账本时两者相同 */
	return snprintf(buf, size, "%u.%02u %u.%02u\n",
			sec, centi, sec, centi);
}
