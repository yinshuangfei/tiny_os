/*
 * touch：若不存在则创建空文件；已存在则更新时间戳（utime）。
 * 用法：touch <path>...
 */
#include "user.h"

static void usage(void)
{
	printf("usage: touch <path>...\n");
}

static int touch_one(const char *path)
{
	int fd;

	if (access(path, F_OK) == 0) {
		if (utime(path, 0) < 0) {
			printf("touch: cannot touch '%s'\n", path);
			return -1;
		}
		return 0;
	}
	fd = open(path, O_WRONLY | O_CREAT);
	if (fd < 0) {
		printf("touch: cannot touch '%s'\n", path);
		return -1;
	}
	close(fd);
	return 0;
}

int main(int argc, char *argv[])
{
	int i, failed;

	if (argc < 2) {
		usage();
		exit(1);
	}

	failed = 0;
	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1] != '\0') {
			if (strcmp(argv[i], "-help") == 0 ||
			    strcmp(argv[i], "--help") == 0) {
				usage();
				exit(0);
			}
			printf("touch: invalid option '%s'\n", argv[i]);
			usage();
			exit(1);
		}
		if (touch_one(argv[i]) < 0)
			failed = 1;
	}
	exit(failed ? 1 : 0);
}
