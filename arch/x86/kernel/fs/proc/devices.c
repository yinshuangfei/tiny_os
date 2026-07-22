/*
 * /proc/devices（对齐 Linux fs/proc/devices.c 教学子集）。
 */
#include "../../types.h"
#include "../../defs.h"
#include "../../driver/chrdev.h"
#include "../../driver/device.h"
#include "internal.h"

struct devices_fill {
	char *buf;
	uint size;
	uint len;
};

static void devices_add(unsigned int major, const char *name, void *arg)
{
	struct devices_fill *f = arg;
	int w;

	if (f->len >= f->size - 1)
		return;
	w = snprintf(f->buf + f->len, f->size - f->len, "%3u %s\n", major, name);
	if (w > 0)
		f->len += (uint)w;
}

int devices_show(char *buf, uint size)
{
	struct devices_fill fill;

	fill.buf = buf;
	fill.size = size;
	fill.len = 0;
	fill.len += (uint)snprintf(buf + fill.len, size - fill.len,
				   "Character devices:\n");
	chrdev_for_each(devices_add, &fill);
	fill.len += (uint)snprintf(buf + fill.len, size - fill.len,
				   "\nBlock devices:\n");
	blkdev_for_each(devices_add, &fill);
	return (int)fill.len;
}
