/*
 * 目录流与少量 POSIX 封装（mkfifo）。
 */
#include "user.h"

struct DIR {
	int fd;
	struct dirent ent;
};

#define NDIR	8
static DIR dir_pool[NDIR];
static char dir_used[NDIR];

DIR *opendir(const char *name)
{
	DIR *d;
	int fd, i;

	if (!name)
		return 0;
	fd = open(name, O_RDONLY);
	if (fd < 0)
		return 0;
	for (i = 0; i < NDIR; i++) {
		if (!dir_used[i]) {
			dir_used[i] = 1;
			d = &dir_pool[i];
			d->fd = fd;
			return d;
		}
	}
	close(fd);
	return 0;
}

struct dirent *readdir(DIR *dirp)
{
	int n;

	if (!dirp || dirp->fd < 0)
		return 0;
	n = read(dirp->fd, &dirp->ent, sizeof(dirp->ent));
	/* 与 sh ls 一致：至少一整条记录；短读或错误则结束 */
	if (n < (int)sizeof(dirp->ent) ||
	    dirp->ent.d_reclen < (unsigned short)sizeof(dirp->ent))
		return 0;
	return &dirp->ent;
}

int closedir(DIR *dirp)
{
	int i;

	if (!dirp)
		return -1;
	if (dirp->fd >= 0)
		close(dirp->fd);
	dirp->fd = -1;
	for (i = 0; i < NDIR; i++) {
		if (dirp == &dir_pool[i]) {
			dir_used[i] = 0;
			return 0;
		}
	}
	return 0;
}

int mkfifo(const char *path, int mode)
{
	return mknod(path, S_IFIFO | (mode & 07777), 0);
}
