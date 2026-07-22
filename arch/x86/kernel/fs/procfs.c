/*
 * 简易 procfs：提供 /proc/meminfo、/proc/cpuinfo（对齐 Linux 教学子集）。
 * 内容在每次 read 时动态生成，不缓存到 inode->data。
 */
#include "../types.h"
#include "../defs.h"
#include "../printk.h"
#include "../mm/memlayout.h"
#include "../mp.h"
#include "../driver/chrdev.h"
#include "../driver/device.h"
#include "fs.h"

#define MEMINFO_BUF	512
#define CPUINFO_BUF	256
#define DEVICES_BUF	512

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
 * /proc/cpuinfo：每 online CPU 一段 "processor : N"。
 * 字段名对齐 Linux，供用户态 get_nprocs(3) 计数（Linux 无 SYS_ncpu）。
 */
static int proc_cpuinfo_read(struct inode *ip, char *dst, uint off, uint n)
{
	char buf[CPUINFO_BUF];
	uint len;
	int i, nc;

	(void)ip;
	nc = num_online_cpus();
	if (nc < 1)
		nc = 1;

	len = 0;
	for (i = 0; i < nc; i++) {
		int w = snprintf(buf + len, sizeof(buf) - len,
				 "processor\t: %d\n", i);
		if (w < 0)
			break;
		len += (uint)w;
		if (len >= sizeof(buf) - 1) {
			len = sizeof(buf) - 1;
			break;
		}
	}

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

static int proc_cpuinfo_write(struct inode *ip, char *src, uint off, uint n)
{
	(void)ip;
	(void)src;
	(void)off;
	(void)n;
	return -1;
}

static void proc_cpuinfo_evict(struct inode *ip)
{
	/* 无私有缓冲 */
	(void)ip;
}

static const struct inode_operations proc_cpuinfo_iops = {
	.read	= proc_cpuinfo_read,
	.write	= proc_cpuinfo_write,
	.evict	= proc_cpuinfo_evict,
};

struct devices_fill {
	char *buf;
	uint size;
	uint len;
};

static void devices_add_chr(unsigned int major, const char *name, void *arg)
{
	struct devices_fill *f = arg;
	int w;

	if (f->len >= f->size - 1)
		return;
	w = snprintf(f->buf + f->len, f->size - f->len, "%3u %s\n", major, name);
	if (w > 0)
		f->len += (uint)w;
}

static void devices_add_blk(unsigned int major, const char *name, void *arg)
{
	devices_add_chr(major, name, arg);
}

/*
 * /proc/devices：对齐 Linux 字符/块主设备号列表。
 */
static int proc_devices_read(struct inode *ip, char *dst, uint off, uint n)
{
	char buf[DEVICES_BUF];
	struct devices_fill fill;
	uint len;

	fill.buf = buf;
	fill.size = sizeof(buf);
	fill.len = 0;

	fill.len += (uint)snprintf(buf + fill.len, sizeof(buf) - fill.len,
				   "Character devices:\n");
	chrdev_for_each(devices_add_chr, &fill);
	fill.len += (uint)snprintf(buf + fill.len, sizeof(buf) - fill.len,
				   "\nBlock devices:\n");
	blkdev_for_each(devices_add_blk, &fill);

	len = fill.len;
	if (len >= sizeof(buf))
		len = sizeof(buf) - 1;
	if (ip)
		ip->size = len;
	if (off >= len)
		return 0;
	if (off + n > len)
		n = len - off;
	memcpy(dst, buf + off, n);
	return (int)n;
}

static int proc_devices_write(struct inode *ip, char *src, uint off, uint n)
{
	(void)ip;
	(void)src;
	(void)off;
	(void)n;
	return -1;
}

static void proc_devices_evict(struct inode *ip)
{
	(void)ip;
}

static const struct inode_operations proc_devices_iops = {
	.read	= proc_devices_read,
	.write	= proc_devices_write,
	.evict	= proc_devices_evict,
};

/*
 * 初始化 procfs，创建 /proc/meminfo、/proc/cpuinfo、/proc/devices。
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

	ip = fs_create("/proc/cpuinfo", T_FILE);
	if (!ip)
		panic("proc_init: /proc/cpuinfo");

	/* 覆盖 ramfs 默认 i_op，改为动态生成 */
	ip->i_op = &proc_cpuinfo_iops;
	ip->data = 0;
	ip->size = 0;
	fs_iput(ip);

	ip = fs_create("/proc/devices", T_FILE);
	if (!ip)
		panic("proc_init: /proc/devices");
	ip->i_op = &proc_devices_iops;
	ip->data = 0;
	ip->size = 0;
	fs_iput(ip);

	printk(KERN_INFO "fs: /proc/meminfo,/proc/cpuinfo,/proc/devices ready\n");
}
