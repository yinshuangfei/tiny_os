/*
 * 简易 procfs：提供 /proc/meminfo（对齐 Linux /proc/meminfo 教学子集）。
 * 内容在每次 read 时动态生成，不缓存到 inode->data。
 */
#include "../types.h"
#include "../defs.h"
#include "../printk.h"
#include "../mm/memlayout.h"
#include "fs.h"

#define MEMINFO_BUF	512

static int proc_meminfo_read(struct inode *ip, char *dst, uint off, uint n)
{
	char buf[MEMINFO_BUF];
	uint total_kb, free_kb, len;

	(void)ip;

	total_kb = (pmm_nr_pages() * (uint)PGSIZE) / 1024;
	free_kb = (pmm_nr_free_pages() * (uint)PGSIZE) / 1024;

	/*
	 * 字段名与单位对齐 Linux；无 page cache / swap 时相关项为 0。
	 * MemAvailable 暂与 MemFree 相同（尚无可回收账本）。
	 */
	len = (uint)snprintf(buf, sizeof(buf),
		"MemTotal:       %8u kB\n"
		"MemFree:        %8u kB\n"
		"MemAvailable:   %8u kB\n"
		"Buffers:        %8u kB\n"
		"Cached:         %8u kB\n"
		"SwapTotal:      %8u kB\n"
		"SwapFree:       %8u kB\n"
		"free/total:     %8u/%u (%u%%)\n",
		total_kb, free_kb, free_kb, 0u, 0u, 0u, 0u, pmm_nr_free_pages(),
		pmm_nr_pages(), pmm_nr_free_pages() * 100 / pmm_nr_pages());

	if (len >= sizeof(buf))
		len = sizeof(buf) - 1;

	/* 供 fstat / 首次 open 后观察；每次读刷新 */
	if (ip)
		ip->size = len;

	if (off >= len)
		return 0;
	if (off + n > len)
		n = len - off;
	memcpy(dst, buf + off, n);
	return (int)n;
}

static int proc_meminfo_write(struct inode *ip, char *src, uint off, uint n)
{
	(void)ip;
	(void)src;
	(void)off;
	(void)n;
	return -1;
}

static void proc_meminfo_evict(struct inode *ip)
{
	/* 无私有缓冲 */
	(void)ip;
}

static const struct inode_operations proc_meminfo_iops = {
	.read	= proc_meminfo_read,
	.write	= proc_meminfo_write,
	.evict	= proc_meminfo_evict,
};

/*
 * 初始化 procfs，创建 /proc/meminfo 文件。
 */
void proc_init(void)
{
	struct inode *ip;

	if (fs_mkdir("/proc") < 0)
		panic("proc_init: /proc");

	ip = fs_create("/proc/meminfo", T_FILE);
	if (!ip)
		panic("proc_init: /proc/meminfo");

	/* 覆盖 ramfs 默认 i_op，改为动态生成 */
	ip->i_op = &proc_meminfo_iops;
	ip->data = 0;
	ip->size = 0;
	fs_iput(ip);

	printk(KERN_INFO "fs: /proc/meminfo ready\n");
}
