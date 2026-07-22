/*
 * /proc/cpuinfo（对齐 Linux fs/proc/cpuinfo.c 教学子集）。
 * 字段名对齐 Linux，供用户态 get_nprocs(3) 计数。
 */
#include "../../types.h"
#include "../../defs.h"
#include "../../mp.h"
#include "internal.h"

int cpuinfo_show(char *buf, uint size)
{
	uint len = 0;
	int i, nc;

	nc = num_online_cpus();
	if (nc < 1)
		nc = 1;
	for (i = 0; i < nc; i++) {
		int w = snprintf(buf + len, size - len, "processor\t: %d\n", i);

		if (w < 0)
			break;
		len += (uint)w;
		if (len >= size - 1) {
			len = size - 1;
			break;
		}
	}
	return (int)len;
}
