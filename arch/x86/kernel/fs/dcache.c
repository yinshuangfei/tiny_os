/*
 * dentry 缓存操作（对齐 Linux fs/dcache.c 教学子集）。
 */
#include "../types.h"
#include "../defs.h"
#include "vfs.h"
#include "dcache.h"

int d_namecmp(const char *a, const char *b)
{
	int i;

	if (!a || !b)
		return a == b ? 0 : (a ? 1 : -1);
	for (i = 0; i < DIRSIZ && a[i] && a[i] == b[i]; i++)
		;
	return a[i] - b[i];
}

void d_namecpy(char *dst, const char *src)
{
	int i;

	if (!dst)
		return;
	if (!src) {
		dst[0] = '\0';
		return;
	}
	for (i = 0; i < DIRSIZ - 1 && src[i]; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

struct dentry *d_lookup(struct inode *dir, const char *name)
{
	struct dentry *d;

	if (!dir || dir->type != T_DIR || !name)
		return 0;
	for (d = dir->dents; d; d = d->next) {
		if (d_namecmp(d->name, name) == 0)
			return d;
	}
	return 0;
}

/* 在目录中添加名为 name、指向 ip 的目录项 */
int d_add(struct inode *dir, const char *name, struct inode *ip)
{
	struct dentry *d;

	if (!dir || dir->type != T_DIR || !name || !name[0] || !ip)
		return -1;
	if (d_lookup(dir, name))
		return -1;
	d = kmalloc(sizeof(*d));
	if (!d)
		return -1;
	d_namecpy(d->name, name);
	d_namecpy(ip->name, name);	/* getcwd / d_path 用 */
	d->ip = fs_idup(ip);
	if (!ip->parent)
		ip->parent = dir;
	d->next = dir->dents;
	dir->dents = d;
	return 0;
}

/* 按名摘除目录项（不检查子树是否为空） */
int d_unlink(struct inode *dir, const char *name)
{
	struct dentry *prev, *d;

	if (!dir || dir->type != T_DIR || !name || !name[0])
		return -1;

	prev = 0;
	for (d = dir->dents; d; prev = d, d = d->next) {
		if (d_namecmp(d->name, name) != 0)
			continue;
		if (prev)
			prev->next = d->next;
		else
			dir->dents = d->next;
		fs_iput(d->ip);
		kfree(d);
		return 0;
	}
	return -1;
}
