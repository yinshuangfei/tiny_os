/*
 * 最小内存文件系统（ramfs）：单棵目录树，无磁盘。
 * 路径解析、创建、inode 读写均在此完成。
 */
#include "../types.h"
#include "../defs.h"
#include "../param.h"
#include "../printk.h"
#include "fs.h"

static struct inode *root;		/* 根inode */
static struct inode inodes[NINODE];	/* 所有inode */

/* inode_type → Linux dirent.d_type */
static unsigned char inode_to_dtype(short type)
{
	switch (type) {
	case T_DIR:
		return DT_DIR;
	case T_FILE:
		return DT_REG;
	case T_DEV:
		return DT_CHR;
	default:
		return DT_UNKNOWN;
	}
}

/* 比较两个目录项名称 */
static int namecmp(const char *a, const char *b)
{
	int i;

	for (i = 0; i < DIRSIZ && a[i] && a[i] == b[i]; i++)
		;
	return a[i] - b[i];
}

/* 复制目录项名称 */
static void namecpy(char *dst, const char *src)
{
	int i;

	for (i = 0; i < DIRSIZ - 1 && src[i]; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

/* 分配一个空闲 inode */
static struct inode *ialloc(short type)
{
	int i;
	struct inode *ip;

	for (i = 0; i < NINODE; i++) {
		ip = &inodes[i];
		if (ip->type == 0) {
			ip->inum = (uint)(i + 1);
			ip->type = type;
			ip->ref = 1;
			ip->size = 0;
			ip->data = 0;
			ip->dents = 0;
			ip->major = 0;
			return ip;
		}
	}
	return 0;
}

/* 增加 inode 引用计数 */
struct inode *fs_idup(struct inode *ip)
{
	if (ip)
		ip->ref++;
	return ip;
}

/* 释放 inode 引用计数 */
void fs_iput(struct inode *ip)
{
	if (!ip)
		return;
	if (ip->ref <= 0)
		panic("fs_iput");

	ip->ref--;
	/* 教学实现：inode 不回收到空闲池以外的彻底释放，
	 * type 保留；无引用且非根时清空数据以便复用。 */
	if (ip->ref == 0 && ip != root) {
		struct dentry *d, *n;

		if (ip->data) {
			kfree(ip->data);
			ip->data = 0;
		}
		for (d = ip->dents; d; d = n) {
			n = d->next;
			fs_iput(d->ip);
			kfree(d);
		}
		ip->dents = 0;
		ip->type = 0;
		ip->size = 0;
		ip->inum = 0;
	}
}

/* 从 path 取出第一个路径名（组件），返回去除 '/' 后的剩余路径；
 * 取出的组件名称写入大小为 DIRSIZ 的缓冲区 name
 */
static const char *skipelem(const char *path, char *name)
{
	const char *s;
	int len;

	while (*path == '/')
		path++;
	if (*path == 0)
		return 0;
	s = path;
	while (*path != '/' && *path != 0)
		path++;
	len = path - s;
	if (len >= DIRSIZ)
		len = DIRSIZ - 1;
	{
		int i;
		for (i = 0; i < len; i++)
			name[i] = s[i];
		name[len] = '\0';
	}
	while (*path == '/')
		path++;
	return path;
}

/* 在父目录中查找一个目录项 */
static struct dentry *dirlookup(struct inode *dp, const char *name)
{
	struct dentry *d;

	if (!dp || dp->type != T_DIR)
		return 0;
	for (d = dp->dents; d; d = d->next) {
		if (namecmp(d->name, name) == 0)
			return d;
	}
	return 0;
}

/* 将一个 inode 添加到父目录的目录项链表中 */
static int dirlink(struct inode *dp, const char *name, struct inode *ip)
{
	struct dentry *d;

	if (!dp || dp->type != T_DIR)
		return -1;
	if (dirlookup(dp, name))
		return -1;
	d = kmalloc(sizeof(*d));
	if (!d)
		return -1;
	namecpy(d->name, name);
	d->ip = fs_idup(ip);
	d->next = dp->dents;
	dp->dents = d;
	return 0;
}

/*
 * 解析路径。
 * - nameiparent != 0 时返回父目录，name 为最后组件（用于 create）。
 * - nameiparent == 0 时返回目标 inode。
 */
static struct inode *namex(const char *path, int nameiparent, char *name)
{
	struct inode *ip, *next;
	struct dentry *de;
	const char *p;

	if (!path || path[0] != '/')
		return 0;

	ip = fs_idup(root);
	p = path;
	while ((p = skipelem(p, name)) != 0) {
		if (ip->type != T_DIR) {
			fs_iput(ip);
			return 0;
		}
		if (nameiparent && *p == 0) {
			/* 停在父目录，name 已是最后组件 */
			/* 占用父目录的 inode 引用 */
			return ip;
		}
		de = dirlookup(ip, name);
		if (!de) {
			/* 目录项不存在，返回 0 */
			fs_iput(ip);
			return 0;
		}
		next = fs_idup(de->ip);
		fs_iput(ip);
		ip = next;
	}
	if (nameiparent) {
		fs_iput(ip);
		return 0;
	}
	return ip;
}

/* 在指定路径查找一个 inode */
struct inode *fs_namei(const char *path)
{
	char name[DIRSIZ];

	return namex(path, 0, name);
}

/* 在指定路径创建一个 inode, 并添加到父目录的目录项链表中 */
struct inode *fs_create(const char *path, short type)
{
	char name[DIRSIZ];
	struct inode *dp, *ip;

	dp = namex(path, 1, name);
	if (!dp)
		return 0;

	if (name[0] == 0) {
		fs_iput(dp);
		return 0;
	}

	/* 目录项已存在，返回 0 */
	if (dirlookup(dp, name)) {
		fs_iput(dp);
		return 0;
	}

	ip = ialloc(type);
	if (!ip) {
		fs_iput(dp);
		return 0;
	}
	if (dirlink(dp, name, ip) < 0) {
		fs_iput(ip);
		fs_iput(dp);
		return 0;
	}
	fs_iput(dp);
	return ip;
}

/* 从目录 inode 按 struct dirent 记录顺序读取（对齐 Linux getdents 记录语义） */
static int fs_readdir(struct inode *ip, char *dst, uint off, uint n)
{
	struct dentry *d;
	struct dirent de;
	uint idx, i, total;
	uint esz = sizeof(struct dirent);

	if (off % esz)
		return -1;

	idx = off / esz;
	total = 0;
	i = 0;
	for (d = ip->dents; d; d = d->next) {
		if (i++ < idx)
			continue;
		if (n < esz)
			break;

		memset(&de, 0, sizeof(de));
		de.d_ino = d->ip ? d->ip->inum : 0;
		de.d_reclen = (unsigned short)esz;
		de.d_off = off + total + esz;
		de.d_type = d->ip ? inode_to_dtype(d->ip->type) : DT_UNKNOWN;
		namecpy(de.d_name, d->name);
		{
			uint j;

			for (j = 0; j < esz; j++)
				dst[j] = ((char *)&de)[j];
		}
		dst += esz;
		n -= esz;
		total += esz;
	}
	return (int)total;
}

/* 从 inode 的数据区读取数据（普通文件或目录） */
int fs_readi(struct inode *ip, char *dst, uint off, uint n)
{
	uint i;

	if (!ip)
		return -1;
	if (ip->type == T_DIR)
		return fs_readdir(ip, dst, off, n);
	if (ip->type != T_FILE)
		return -1;
	if (off > ip->size)
		return 0;
	if (off + n > ip->size)
		n = ip->size - off;
	if (!ip->data)
		return 0;
	for (i = 0; i < n; i++)
		dst[i] = ip->data[off + i];
	return (int)n;
}

/* 将数据写入 inode 的数据区 */
int fs_writei(struct inode *ip, char *src, uint off, uint n)
{
	uint newsize, i;
	char *buf;

	if (!ip || ip->type != T_FILE)
		return -1;
	if (n == 0)
		return 0;
	newsize = off + n;

	/* 如果文件大小不足，或者没有数据缓冲区，则分配新的缓冲区 */
	if (newsize > ip->size || !ip->data) {
		buf = kmalloc(newsize ? newsize : 1);
		if (!buf)
			return -1;

		/* 将旧数据复制到新缓冲区 */
		if (ip->data) {
			for (i = 0; i < ip->size; i++)
				buf[i] = ip->data[i];
			kfree(ip->data);
		}

		/* 将新缓冲区初始化为 0 */
		for (i = ip->size; i < newsize; i++)
			buf[i] = 0;

		ip->data = buf;
		ip->size = newsize;
	}
	/* 将新数据写入缓冲区 */
	for (i = 0; i < n; i++)
		ip->data[off + i] = src[i];
	/* 更新文件大小 */
	if (off + n > ip->size)
		ip->size = off + n;
	return (int)n;
}

/* 在指定路径创建一个文件，并写入数据 */
static void seed_file(const char *path, const char *text)
{
	struct inode *ip;
	uint n;

	ip = fs_create(path, T_FILE);
	if (!ip)
		return;
	for (n = 0; text[n]; n++)
		;
	fs_writei(ip, (char *)text, 0, n);
	fs_iput(ip);
}

/* 初始化文件系统 */
void fs_init(void)
{
	struct inode *dev, *console;

	memset(inodes, 0, sizeof(inodes));

	/* 根inode */
	root = ialloc(T_DIR);
	if (!root)
		panic("fs_init: root");

	/* /dev */
	dev = ialloc(T_DIR);
	if (!dev || dirlink(root, "dev", dev) < 0)
		panic("fs_init: /dev");
	fs_iput(dev);

	console = ialloc(T_DEV);
	if (!console)
		panic("fs_init: console");
	console->major = DEV_CONSOLE;
	if (dirlink(dev, "console", console) < 0)
		panic("fs_init: /dev/console");
	fs_iput(console);

	seed_file("/hello", "Hello from Tiny-OS ramfs!\n");

	fileinit();
	printk(KERN_INFO "fs: ramfs ready (root, /dev/console, /hello)\n");
}
