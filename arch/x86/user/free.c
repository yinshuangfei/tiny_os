/*
 * free：读取 /proc/meminfo，打印类似 Linux free 的摘要（仅 Mem 行）。
 * 支持 -h：人类可读单位（Ki / Mi / Gi，1024 进制）。
 */
#include "user.h"

#define MEMINFO_PATH	"/proc/meminfo"
#define BUFSIZE		512

static int starts_with(const char *s, const char *prefix)
{
	while (*prefix) {
		if (*s++ != *prefix++)
			return 0;
	}
	return 1;
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

/* 跳过空白与冒号，解析十进制无符号整数 */
static int parse_kb(const char *line, unsigned int *out)
{
	unsigned int v;

	while (*line && (*line < '0' || *line > '9'))
		line++;
	if (*line < '0' || *line > '9')
		return -1;
	v = 0;
	while (*line >= '0' && *line <= '9') {
		v = v * 10 + (unsigned int)(*line - '0');
		line++;
	}
	*out = v;
	return 0;
}

static int read_meminfo(unsigned int *total_kb, unsigned int *free_kb)
{
	char buf[BUFSIZE];
	char line[128];
	int fd, n, i, li;
	int got_total, got_free;

	fd = open(MEMINFO_PATH, O_RDONLY);
	if (fd < 0)
		return -1;

	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = '\0';

	got_total = 0;
	got_free = 0;
	li = 0;
	for (i = 0; i <= n; i++) {
		char c = (i < n) ? buf[i] : '\n';

		if (c == '\n' || c == '\0') {
			line[li] = '\0';
			if (li > 0) {
				if (starts_with(line, "MemTotal:") &&
				    parse_kb(line, total_kb) == 0)
					got_total = 1;
				else if (starts_with(line, "MemFree:") &&
					 parse_kb(line, free_kb) == 0)
					got_free = 1;
			}
			li = 0;
			if (got_total && got_free)
				return 0;
			continue;
		}
		if (li < (int)sizeof(line) - 1)
			line[li++] = c;
	}
	return (got_total && got_free) ? 0 : -1;
}

/*
 * 将 kB 格式化为人类可读串（对齐 procps free -h 风格）。
 * out 至少 16 字节。
 */
static void fmt_human(unsigned int kb, char *out, unsigned int outsz)
{
	unsigned int whole, frac, div;
	const char *unit;

	if (kb >= 1024u * 1024u) {
		div = 1024u * 1024u;
		unit = "Gi";
	} else if (kb >= 1024u) {
		div = 1024u;
		unit = "Mi";
	} else {
		snprintf(out, outsz, "%uKi", kb);
		return;
	}

	whole = kb / div;
	frac = ((kb % div) * 10u) / div;
	if (frac)
		snprintf(out, outsz, "%u.%u%s", whole, frac, unit);
	else
		snprintf(out, outsz, "%u%s", whole, unit);
}

static void usage(void)
{
	printf("usage: free [-h]\n");
}

int main(int argc, char *argv[])
{
	unsigned int total_kb, free_kb, used_kb;
	int human, i;
	char tbuf[16], ubuf[16], fbuf[16];

	human = 0;
	for (i = 1; i < argc; i++) {
		if (streq(argv[i], "-h")) {
			human = 1;
		} else if (streq(argv[i], "-help") || streq(argv[i], "--help")) {
			usage();
			exit(0);
		} else {
			printf("free: invalid option '%s'\n", argv[i]);
			usage();
			exit(1);
		}
	}

	if (read_meminfo(&total_kb, &free_kb) < 0) {
		printf("free: cannot read %s\n", MEMINFO_PATH);
		exit(1);
	}
	used_kb = (total_kb >= free_kb) ? (total_kb - free_kb) : 0;

	printf("%20s %11s %11s\n", "total", "used", "free");
	if (human) {
		fmt_human(total_kb, tbuf, sizeof(tbuf));
		fmt_human(used_kb, ubuf, sizeof(ubuf));
		fmt_human(free_kb, fbuf, sizeof(fbuf));
		printf("Mem:     %11s %11s %11s\n", tbuf, ubuf, fbuf);
	} else {
		printf("Mem:     %11u %11u %11u\n", total_kb, used_kb, free_kb);
	}
	exit(0);
}
