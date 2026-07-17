/*
 * ramfs：内存文件系统后端（对齐 Linux fs/ramfs）。
 * 只实现 inode_operations；VFS 编排见 namei.c / inode.c / file.c。
 */
#include "../types.h"
#include "../defs.h"
#include "../param.h"
#include "../printk.h"
#include "fs.h"

/* 内存文件系统 inode 数组 */
static struct inode inodes[NINODE];

static struct inode *ramfs_lookup(struct inode *dir, const char *name);
static int ramfs_create(struct inode *dir, const char *name, short type,
			struct inode **out);
static int ramfs_mkdir(struct inode *dir, const char *name);
static int ramfs_rmdir(struct inode *dir, const char *name);
static int ramfs_mknod(struct inode *dir, const char *name, short type,
		       dev_t rdev);
static int ramfs_get_name(struct inode *dir, struct inode *child, char *name);
static void ramfs_evict(struct inode *ip);
static int ramfs_read(struct inode *ip, char *dst, uint off, uint n);
static int ramfs_write(struct inode *ip, char *src, uint off, uint n);

static const struct inode_operations ramfs_iops = {
	.lookup		= ramfs_lookup,
	.create		= ramfs_create,
	.mkdir		= ramfs_mkdir,
	.rmdir		= ramfs_rmdir,
	.mknod		= ramfs_mknod,
	.get_name	= ramfs_get_name,
	.evict		= ramfs_evict,
	.read		= ramfs_read,
	.write		= ramfs_write,
};

/* inode_type → Linux dirent.d_type */
static unsigned char inode_to_dtype(short type)
{
	switch (type) {
	case T_DIR:
		return DT_DIR;
	case T_FILE:
		return DT_REG;
	case T_CHAR:
		return DT_CHR;
	case T_BLK:
		return DT_BLK;
	default:
		return DT_UNKNOWN;
	}
}

static int namecmp(const char *a, const char *b)
{
	int i;

	for (i = 0; i < DIRSIZ && a[i] && a[i] == b[i]; i++)
		;
	return a[i] - b[i];
}

static void namecpy(char *dst, const char *src)
{
	int i;

	for (i = 0; i < DIRSIZ - 1 && src[i]; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

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
			ip->parent = 0;
			ip->rdev = 0;
			ip->i_op = &ramfs_iops;
			return ip;
		}
	}
	return 0;
}

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

/* 在目录中添加一个名为 name，inode 为 ip 的目录项 */
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
	if (!ip->parent)
		ip->parent = dp;
	d->next = dp->dents;
	dp->dents = d;
	return 0;
}

/* 供其它 FS 挂接到 ramfs 目录（如 ext2 → /mnt） */
int ramfs_link(struct inode *dir, const char *name, struct inode *ip)
{
	return dirlink(dir, name, ip);
}

static int dirunlink(struct inode *dp, const char *name)
{
	struct dentry *prev, *d;

	if (!dp || dp->type != T_DIR || !name || !name[0])
		return -1;

	prev = 0;
	for (d = dp->dents; d; prev = d, d = d->next) {
		if (namecmp(d->name, name) != 0)
			continue;
		if (prev)
			prev->next = d->next;
		else
			dp->dents = d->next;
		fs_iput(d->ip);
		kfree(d);
		return 0;
	}
	return -1;
}

/* ---------- inode_operations ---------- */

static struct inode *ramfs_lookup(struct inode *dir, const char *name)
{
	struct dentry *de;

	de = dirlookup(dir, name);
	if (!de)
		return 0;
	return fs_idup(de->ip);
}

static int ramfs_create(struct inode *dir, const char *name, short type,
			struct inode **out)
{
	struct inode *ip;

	if (!out || dirlookup(dir, name))
		return -1;
	ip = ialloc(type);
	if (!ip)
		return -1;
	if (dirlink(dir, name, ip) < 0) {
		fs_iput(ip);	/* ref→0 → evict */
		return -1;
	}
	*out = ip;	/* 调用方持有 ialloc 的那份引用 */
	return 0;
}

static int ramfs_mkdir(struct inode *dir, const char *name)
{
	struct inode *ip;

	if (dirlookup(dir, name))
		return -1;
	ip = ialloc(T_DIR);
	if (!ip)
		return -1;
	if (dirlink(dir, name, ip) < 0) {
		fs_iput(ip);
		return -1;
	}
	fs_iput(ip);	/* 仅保留目录项引用 */
	return 0;
}

static int ramfs_rmdir(struct inode *dir, const char *name)
{
	struct dentry *de;
	struct inode *ip;

	de = dirlookup(dir, name);
	if (!de)
		return -1;
	ip = de->ip;
	if (ip->type != T_DIR || ip->dents)
		return -1;
	return dirunlink(dir, name);
}

static int ramfs_mknod(struct inode *dir, const char *name, short type,
		       dev_t rdev)
{
	struct inode *ip;

	if (type != T_CHAR && type != T_BLK)
		return -1;
	if (dirlookup(dir, name))
		return -1;
	ip = ialloc(type);
	if (!ip)
		return -1;
	ip->rdev = rdev;
	if (dirlink(dir, name, ip) < 0) {
		fs_iput(ip);
		return -1;
	}
	fs_iput(ip);
	return 0;
}

static int ramfs_get_name(struct inode *dir, struct inode *child, char *name)
{
	struct dentry *d;

	if (!dir || !child || dir->type != T_DIR)
		return -1;
	for (d = dir->dents; d; d = d->next) {
		if (d->ip == child) {
			namecpy(name, d->name);
			return 0;
		}
	}
	return -1;
}

static void ramfs_evict(struct inode *ip)
{
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
	ip->parent = 0;
	ip->i_op = 0;
	ip->rdev = 0;
	ip->type = 0;
	ip->size = 0;
	ip->inum = 0;
}

static int ramfs_readdir(struct inode *ip, char *dst, uint off, uint n)
{
	struct dentry *d;
	struct dirent de;
	uint idx, i, total, j;
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
		for (j = 0; j < esz; j++)
			dst[j] = ((char *)&de)[j];
		dst += esz;
		n -= esz;
		total += esz;
	}
	return (int)total;
}

static int ramfs_read(struct inode *ip, char *dst, uint off, uint n)
{
	uint i;

	if (!ip)
		return -1;
	if (ip->type == T_DIR)
		return ramfs_readdir(ip, dst, off, n);
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

static int ramfs_write(struct inode *ip, char *src, uint off, uint n)
{
	uint newsize, i;
	char *buf;

	if (!ip || ip->type != T_FILE)
		return -1;
	if (n == 0)
		return 0;
	newsize = off + n;

	if (newsize > ip->size || !ip->data) {
		buf = kmalloc(newsize ? newsize : 1);
		if (!buf)
			return -1;
		if (ip->data) {
			for (i = 0; i < ip->size; i++)
				buf[i] = ip->data[i];
			kfree(ip->data);
		}
		for (i = ip->size; i < newsize; i++)
			buf[i] = 0;
		ip->data = buf;
		ip->size = newsize;
	}
	for (i = 0; i < n; i++)
		ip->data[off + i] = src[i];
	if (off + n > ip->size)
		ip->size = off + n;
	return (int)n;
}

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

void fs_init(void)
{
	struct inode *root, *dev, *console;

	memset(inodes, 0, sizeof(inodes));

	root = ialloc(T_DIR);
	if (!root)
		panic("fs_init: root");
	root->parent = root;
	vfs_set_root(root);

	dev = ialloc(T_DIR);
	if (!dev || dirlink(root, "dev", dev) < 0)
		panic("fs_init: /dev");
	fs_iput(dev);

	console = ialloc(T_CHAR);
	if (!console)
		panic("fs_init: console");
	console->rdev = MKDEV(CONSOLE_MAJOR, CONSOLE_MINOR);
	if (dirlink(dev, "console", console) < 0)
		panic("fs_init: /dev/console");
	fs_iput(console);

	seed_file("/hello", "Hello from Tiny-OS ramfs!\n");

	fileinit();
	printk(KERN_INFO "fs: VFS+ramfs ready (root, /dev, /hello)\n");

	proc_init();
}
