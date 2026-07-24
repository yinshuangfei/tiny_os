/*
 * /proc/partitions（对齐 Linux 主次设备号 + 1K 块数 + 盘名）。
 */
#include "../../types.h"
#include "../../defs.h"
#include "../../ide.h"
#include "../../block/blk.h"
#include "internal.h"

int partitions_show(char *buf, uint size)
{
	uint len = 0;
	int i, n;

	len += (uint)snprintf(buf + len, size - len,
			      "major minor  #blocks  name\n\n");
	n = ide_ndisks();
	for (i = 0; i < n; i++) {
		struct gendisk *gd = ide_gendisk(i);
		sector_t sect;
		uint blocks;

		if (!gd)
			continue;
		sect = get_capacity(gd);
		/* Linux #blocks 以 1KiB 计；扇区 512B → /2 */
		blocks = (uint)(sect / 2);
		if (len >= size - 1)
			break;
		len += (uint)snprintf(buf + len, size - len,
				      "%4d %5d %8u %s\n",
				      gd->major, gd->first_minor,
				      blocks, gd->disk_name);
	}
	return (int)len;
}
