/*
 * ramfs：内存文件系统后端（对齐 Linux fs/ramfs）。
 * 只实现 inode_operations；VFS 编排见 namei.c / inode.c / dcache.c / file.c。
 */
#include "../../types.h"
#include "../../defs.h"
#include "../../param.h"
#include "../../printk.h"
#include "../fs.h"
#include "../dcache.h"
#include "ramfs.h"

/* 内存文件系统 inode 数组 */
static struct inode inodes[NINODE];

static struct inode *ramfs_lookup(struct inode *dir, const char *name);
static int ramfs_create(struct inode *dir, const char *name, short type,
			struct inode **out);
static int ramfs_mkdir(struct inode *dir, const char *name);
static int ramfs_rmdir(struct inode *dir, const char *name);
static int ramfs_mknod(struct inode *dir, const char *name, short type,
		       dev_t rdev);
static int ramfs_symlink(struct inode *dir, const char *name,
			 const char *target);
static int ramfs_hardlink(struct inode *dir, const char *name, struct inode *ip);
static int ramfs_unlink(struct inode *dir, const char *name);
static int ramfs_rename(struct inode *old_dir, const char *old_name,
			struct inode *new_dir, const char *new_name);
static void ramfs_evict(struct inode *ip);
static int ramfs_read(struct inode *ip, char *dst, uint off, uint n);
static int ramfs_write(struct inode *ip, char *src, uint off, uint n);

static const struct inode_operations ramfs_iops = {
	.lookup		= ramfs_lookup,
	.create		= ramfs_create,
	.mkdir		= ramfs_mkdir,
	.rmdir		= ramfs_rmdir,
	.mknod		= ramfs_mknod,
	.symlink	= ramfs_symlink,
	.link		= ramfs_hardlink,
	.unlink		= ramfs_unlink,
	.rename		= ramfs_rename,
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
	case T_FIFO:
		return DT_FIFO;
	case T_LNK:
		return DT_LNK;
	default:
		return DT_UNKNOWN;
	}
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
			ip->i_pipe = 0;
			ip->dents = 0;
			ip->parent = 0;
			ip->rdev = 0;
			ip->i_sb = 0;
			ip->name[0] = '\0';
			ip->i_op = &ramfs_iops;
			return ip;
		}
	}
	return 0;
}

/* 供其它 FS 挂接到 ramfs 目录（如 ext2 → /mnt） */
int ramfs_link(struct inode *dir, const char *name, struct inode *ip)
{
	return d_add(dir, name, ip);
}

/* umount：摘除挂载点目录项（不检查是否为空） */
int ramfs_detach(struct inode *dir, const char *name)
{
	return d_unlink(dir, name);
}

/* ---------- inode_operations ---------- */

static struct inode *ramfs_lookup(struct inode *dir, const char *name)
{
	struct dentry *de;

	de = d_lookup(dir, name);
	if (!de)
		return 0;
	return fs_idup(de->ip);
}

static int ramfs_create(struct inode *dir, const char *name, short type,
			struct inode **out)
{
	struct inode *ip;

	if (!out || d_lookup(dir, name))
		return -1;
	ip = ialloc(type);
	if (!ip)
		return -1;
	if (d_add(dir, name, ip) < 0) {
		fs_iput(ip);	/* ref→0 → evict */
		return -1;
	}
	*out = ip;	/* 调用方持有 ialloc 的那份引用 */
	return 0;
}

static int ramfs_mkdir(struct inode *dir, const char *name)
{
	struct inode *ip;

	if (d_lookup(dir, name))
		return -1;
	ip = ialloc(T_DIR);
	if (!ip)
		return -1;
	if (d_add(dir, name, ip) < 0) {
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

	de = d_lookup(dir, name);
	if (!de)
		return -1;
	ip = de->ip;
	if (ip->type != T_DIR || ip->dents)
		return -1;
	return d_unlink(dir, name);
}

static int ramfs_mknod(struct inode *dir, const char *name, short type,
		       dev_t rdev)
{
	struct inode *ip;

	if (type != T_CHAR && type != T_BLK)
		return -1;
	if (d_lookup(dir, name))
		return -1;
	ip = ialloc(type);
	if (!ip)
		return -1;
	ip->rdev = rdev;
	if (d_add(dir, name, ip) < 0) {
		fs_iput(ip);
		return -1;
	}
	fs_iput(ip);
	return 0;
}

static int ramfs_symlink(struct inode *dir, const char *name,
			 const char *target)
{
	struct inode *ip;
	uint len;

	if (!dir || !name || !name[0] || !target || dir->type != T_DIR)
		return -1;
	if (d_lookup(dir, name))
		return -1;
	len = strlen(target);
	ip = ialloc(T_LNK);
	if (!ip)
		return -1;
	ip->data = kmalloc(len + 1);
	if (!ip->data) {
		fs_iput(ip);
		return -1;
	}
	strcpy(ip->data, target);
	ip->size = len;
	if (d_add(dir, name, ip) < 0) {
		fs_iput(ip);
		return -1;
	}
	fs_iput(ip);
	return 0;
}

/* 硬链接（i_op->link）；与对外挂接用的 ramfs_link() 分离 */
static int ramfs_hardlink(struct inode *dir, const char *name, struct inode *ip)
{
	if (!dir || !ip || dir->type != T_DIR)
		return -1;
	if (ip->type == T_DIR)
		return -1;
	return d_add(dir, name, ip);
}

static int ramfs_unlink(struct inode *dir, const char *name)
{
	struct dentry *de;

	de = d_lookup(dir, name);
	if (!de)
		return -1;
	if (de->ip->type == T_DIR)
		return -1;
	return d_unlink(dir, name);
}

static int ramfs_rename(struct inode *old_dir, const char *old_name,
			struct inode *new_dir, const char *new_name)
{
	struct dentry *de, *nde;
	struct inode *ip;

	if (!old_dir || !new_dir || old_dir->type != T_DIR ||
	    new_dir->type != T_DIR)
		return -1;

	de = d_lookup(old_dir, old_name);
	if (!de)
		return -1;
	ip = de->ip;

	/* 同目录同名：无操作 */
	if (old_dir == new_dir && d_namecmp(old_name, new_name) == 0)
		return 0;

	nde = d_lookup(new_dir, new_name);
	if (nde) {
		/* 目标已存在：仅允许覆盖普通文件；目录须为空且源也是目录 */
		if (nde->ip == ip)
			return 0;
		if (nde->ip->type == T_DIR) {
			if (ip->type != T_DIR || nde->ip->dents)
				return -1;
		} else if (ip->type == T_DIR) {
			return -1;
		}
		if (d_unlink(new_dir, new_name) < 0)
			return -1;
	}

	/* 同目录：直接改名 */
	if (old_dir == new_dir) {
		d_namecpy(de->name, new_name);
		d_namecpy(ip->name, new_name);
		return 0;
	}

	/* 跨目录：摘链再挂接（持临时引用防止中间被回收） */
	ip = fs_idup(ip);
	if (d_unlink(old_dir, old_name) < 0) {
		fs_iput(ip);
		return -1;
	}
	if (d_add(new_dir, new_name, ip) < 0) {
		(void)d_add(old_dir, old_name, ip);
		fs_iput(ip);
		return -1;
	}
	if (ip->type == T_DIR)
		ip->parent = new_dir;
	fs_iput(ip);
	return 0;
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
	ip->name[0] = '\0';
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
		d_namecpy(de.d_name, d->name);
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
	if (ip->type != T_FILE && ip->type != T_LNK)
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
	if (!dev || d_add(root, "dev", dev) < 0)
		panic("fs_init: /dev");
	fs_iput(dev);

	console = ialloc(T_CHAR);
	if (!console)
		panic("fs_init: console");
	console->rdev = MKDEV(CONSOLE_MAJOR, CONSOLE_MINOR);
	if (d_add(dev, "console", console) < 0)
		panic("fs_init: /dev/console");
	fs_iput(console);

	seed_file("/hello", "Hello from Tiny-OS ramfs!\n");

	fileinit();
	printk(KERN_INFO "fs: VFS+ramfs ready (root, /dev, /hello)\n");

	proc_init();
}
