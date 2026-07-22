/*
 * /proc/meminfo（对齐 Linux fs/proc/meminfo.c 教学子集）。
 */
#include "../../types.h"
#include "../../defs.h"
#include "../../mm/memlayout.h"
#include "internal.h"

int meminfo_show(char *buf, uint size)
{
	uint total_kb, free_kb;

	total_kb = (pmm_nr_pages() * (uint)PGSIZE) / 1024;
	free_kb = (pmm_nr_free_pages() * (uint)PGSIZE) / 1024;
	/*
	 * 字段名对齐 Linux；无 page cache / swap 时相关项为 0。
	 * MemAvailable 暂与 MemFree 相同。
	 */
	return snprintf(buf, size,
		"MemTotal:       %8u kB\n"
		"MemFree:        %8u kB\n"
		"MemAvailable:   %8u kB\n"
		"Buffers:        %8u kB\n"
		"Cached:         %8u kB\n"
		"SwapTotal:      %8u kB\n"
		"SwapFree:       %8u kB\n"
		"free/total:     %8u/%u (%u%%)\n",
		total_kb, free_kb, free_kb, 0u, 0u, 0u, 0u,
		pmm_nr_free_pages(), pmm_nr_pages(),
		pmm_nr_free_pages() * 100 / pmm_nr_pages());
}
