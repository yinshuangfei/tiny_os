/*
 * /proc/mounts（对齐 Linux：device mountpoint type options dump pass）。
 */
#include "../../types.h"
#include "../../defs.h"
#include "../mount.h"
#include "internal.h"

int mounts_show(char *buf, uint size)
{
	return mounts_proc_show(buf, size);
}
