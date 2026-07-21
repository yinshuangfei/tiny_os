/*
 * 对齐 Linux 的 CPU 查询辅助（非独立系统调用）：
 *   sched_getcpu(3) → getcpu(2)
 *   get_nprocs(3)   → 数 /proc/cpuinfo 中的 processor 行
 */
#include "user.h"
#include "syscall.h"

int sched_getcpu(void)
{
	unsigned int cpu;

	if (getcpu(&cpu, 0) < 0)
		return -1;
	return (int)cpu;
}

/* 暴力解析 /proc/cpuinfo 文件，统计 CPU 数量 */
int get_nprocs(void)
{
	char buf[256];
	int fd, n, i, cpus;
	char prev;

	fd = open("/proc/cpuinfo", O_RDONLY);
	if (fd < 0)
		return 1;

	cpus = 0;
	prev = '\n';
	for (;;) {
		n = read(fd, buf, sizeof(buf));
		if (n <= 0)
			break;
		for (i = 0; i < n; i++) {
			/*
			 * 行首 "processor"：与 Linux /proc/cpuinfo 字段名一致。
			 */
			if (prev == '\n' && i + 9 <= n &&
			    buf[i] == 'p' && buf[i + 1] == 'r' &&
			    buf[i + 2] == 'o' && buf[i + 3] == 'c' &&
			    buf[i + 4] == 'e' && buf[i + 5] == 's' &&
			    buf[i + 6] == 's' && buf[i + 7] == 'o' &&
			    buf[i + 8] == 'r')
				cpus++;
			prev = buf[i];
		}
	}
	close(fd);
	return cpus > 0 ? cpus : 1;
}
