/*
 * VFS：路径解析与目录操作编排（对齐 Linux fs/namei.c）。
 * 不触及具体存储；目录项查找/创建/删除经 inode->i_op。
 */
#include "../types.h"
#include "../defs.h"
#include "../param.h"
#include "../proc.h"
#include "vfs.h"
#include "dcache.h"
#include "namei.h"

#define MAXSYMLINKS	8

/* 从 path 取出第一个路径分量到 name，返回剩余路径；结束则返回 0 */
static const char *skipelem(const char *path, char *name)
{
	const char *s;
	int len, i;

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
	for (i = 0; i < len; i++)
		name[i] = s[i];
	name[len] = '\0';
	while (*path == '/')
		path++;
	return path;
}

/* 教学：仅支持单分量相对目标（如 /proc/self → "N"） */
static struct inode *follow_symlink(struct inode *link, int *depth)
{
	char target[DIRSIZ];
	struct inode *parent, *next;
	int n, i;

	if (++(*depth) > MAXSYMLINKS) {
		fs_iput(link);
		return 0;
	}
	n = fs_readi(link, target, 0, DIRSIZ - 1);
	if (n <= 0) {
		fs_iput(link);
		return 0;
	}
	target[n] = '\0';
	if (target[0] == '/') {
		fs_iput(link);
		return 0;
	}
	for (i = 0; target[i]; i++) {
		if (target[i] == '/') {
			fs_iput(link);
			return 0;
		}
	}
	parent = link->parent;
	if (!parent || !parent->i_op || !parent->i_op->lookup) {
		fs_iput(link);
		return 0;
	}
	parent = fs_idup(parent);
	fs_iput(link);
	next = parent->i_op->lookup(parent, target);
	fs_iput(parent);
	return next;
}

/*
 * 解析路径。
 * - 以 '/' 开头：从 vfs_root；否则从当前进程 cwd。
 * - 支持 "." / ".."。
 * - follow：末分量是否跟随符号链接（open/stat=1，readlink=0）；中间分量总是跟随。
 * - nameiparent != 0 时返回父目录，name 为最后组件。
 * - nameiparent == 0 时返回目标 inode。
 */
static struct inode *namex(const char *path, int nameiparent, char *name,
			   int follow)
{
	struct inode *ip, *next;
	struct proc *p;
	const char *s;
	int depth;

	if (!path || path[0] == 0)
		return 0;

	if (path[0] == '/') {
		if (!vfs_root())
			return 0;
		ip = fs_idup(vfs_root());
	} else {
		p = myproc();
		if (!p || !p->cwd)
			return 0;
		ip = fs_idup(p->cwd);
	}

	s = path;
	while ((s = skipelem(s, name)) != 0) {
		if (ip->type != T_DIR) {
			fs_iput(ip);
			return 0;
		}
		if (nameiparent && *s == 0)
			return ip;
		if (d_namecmp(name, ".") == 0)
			continue;
		if (d_namecmp(name, "..") == 0) {
			next = ip->parent ? ip->parent : ip;
			next = fs_idup(next);
			fs_iput(ip);
			ip = next;
			continue;
		}
		if (!ip->i_op || !ip->i_op->lookup) {
			fs_iput(ip);
			return 0;
		}
		next = ip->i_op->lookup(ip, name);
		fs_iput(ip);
		if (!next)
			return 0;

		depth = 0;
		while (next->type == T_LNK) {
			/* 末分量且 nofollow：留给 readlink */
			if (*s == 0 && !follow)
				break;
			next = follow_symlink(next, &depth);
			if (!next)
				return 0;
		}
		ip = next;
	}
	if (nameiparent) {
		fs_iput(ip);
		return 0;
	}
	return ip;
}

/* 根据路径查找 inode（跟随符号链接） */
struct inode *fs_namei(const char *path)
{
	char name[DIRSIZ];

	return namex(path, 0, name, 1);
}

/* 不跟随末分量符号链接（供 readlink） */
struct inode *fs_namei_nofollow(const char *path)
{
	char name[DIRSIZ];

	return namex(path, 0, name, 0);
}

/*
 * getcwd：沿 parent 链用各 inode->name 拼路径（对齐 Linux d_path / dentry.d_name）。
 * 不经 i_op（Linux inode_operations 无 get_name）。
 */
int fs_getcwd(char *buf, int max)
{
	struct proc *p = myproc();
	struct inode *ip, *stack[NINODE];
	struct inode *root;
	int nstack, i, len, nlen, j;

	root = vfs_root();
	if (!buf || max < 2 || !p || !p->cwd || !root)
		return -1;

	ip = p->cwd;
	nstack = 0;
	while (ip && ip != root && ip->parent && ip->parent != ip) {
		if (nstack >= NINODE)
			return -1;
		stack[nstack++] = ip;
		ip = ip->parent;
	}

	if (nstack == 0) {
		buf[0] = '/';
		buf[1] = '\0';
		return 2;
	}

	len = 0;
	for (i = nstack - 1; i >= 0; i--) {
		if (!stack[i]->name[0])
			return -1;
		nlen = 0;
		while (stack[i]->name[nlen])
			nlen++;
		if (len + 1 + nlen + 1 > max)
			return -1;
		buf[len++] = '/';
		for (j = 0; j < nlen; j++)
			buf[len++] = stack[i]->name[j];
	}
	buf[len++] = '\0';
	return len;
}

struct inode *fs_create(const char *path, short type)
{
	char name[DIRSIZ];
	struct inode *dp, *ip;

	dp = namex(path, 1, name, 1);
	if (!dp)
		return 0;
	if (name[0] == 0) {
		fs_iput(dp);
		return 0;
	}
	if (!dp->i_op || !dp->i_op->create) {
		fs_iput(dp);
		return 0;
	}
	if (dp->i_op->create(dp, name, type, &ip) < 0) {
		fs_iput(dp);
		return 0;
	}
	fs_iput(dp);
	return ip;
}

struct inode *fs_mknod(const char *path, short type,
		       unsigned int major, unsigned int minor)
{
	char name[DIRSIZ];
	struct inode *dp;
	dev_t rdev;

	if (type != T_CHAR && type != T_BLK)
		return 0;

	dp = namex(path, 1, name, 1);
	if (!dp)
		return 0;
	if (name[0] == 0 || !dp->i_op || !dp->i_op->mknod) {
		fs_iput(dp);
		return 0;
	}
	rdev = MKDEV(major, minor);
	if (dp->i_op->mknod(dp, name, type, rdev) < 0) {
		fs_iput(dp);
		return 0;
	}
	fs_iput(dp);
	/* 再 lookup 一次以返回持引用的 inode（与旧 fs_create 语义一致） */
	return fs_namei(path);
}

static int inode_is_cwd(struct inode *ip)
{
	int i;
	struct proc *p;

	if (!ip || !proc_table)
		return 0;
	for (i = 0; i < NPROC; i++) {
		p = &proc_table[i];
		if (p->state != UNUSED && p->cwd == ip)
			return 1;
	}
	return 0;
}

int fs_mkdir(const char *path)
{
	char name[DIRSIZ];
	struct inode *dp;

	if (!path || !path[0])
		return -1;

	dp = namex(path, 1, name, 1);
	if (!dp)
		return -1;
	if (name[0] == 0 ||
	    d_namecmp(name, ".") == 0 ||
	    d_namecmp(name, "..") == 0) {
		fs_iput(dp);
		return -1;
	}
	if (!dp->i_op || !dp->i_op->mkdir) {
		fs_iput(dp);
		return -1;
	}
	if (dp->i_op->mkdir(dp, name) < 0) {
		fs_iput(dp);
		return -1;
	}
	fs_iput(dp);
	return 0;
}

int fs_rmdir(const char *path)
{
	char name[DIRSIZ];
	struct inode *dp, *ip;

	if (!path || !path[0])
		return -1;

	dp = namex(path, 1, name, 1);
	if (!dp)
		return -1;
	if (name[0] == 0 ||
	    d_namecmp(name, ".") == 0 ||
	    d_namecmp(name, "..") == 0) {
		fs_iput(dp);
		return -1;
	}
	if (!dp->i_op || !dp->i_op->lookup || !dp->i_op->rmdir) {
		fs_iput(dp);
		return -1;
	}

	ip = dp->i_op->lookup(dp, name);
	if (!ip) {
		fs_iput(dp);
		return -1;
	}
	/* 空目录检查由后端 rmdir 完成；此处做 VFS 策略 */
	if (ip->type != T_DIR || ip == vfs_root() || inode_is_cwd(ip)) {
		fs_iput(ip);
		fs_iput(dp);
		return -1;
	}
	fs_iput(ip);

	if (dp->i_op->rmdir(dp, name) < 0) {
		fs_iput(dp);
		return -1;
	}
	fs_iput(dp);
	return 0;
}

/* link(old, new)：为已有 inode 增加硬链接（不可链接目录） */
int fs_link(const char *oldpath, const char *newpath)
{
	char name[DIRSIZ];
	struct inode *ip, *dp;

	if (!oldpath || !oldpath[0] || !newpath || !newpath[0])
		return -1;

	ip = fs_namei(oldpath);
	if (!ip)
		return -1;
	if (ip->type == T_DIR) {
		fs_iput(ip);
		return -1;
	}

	dp = namex(newpath, 1, name, 1);
	if (!dp) {
		fs_iput(ip);
		return -1;
	}
	if (name[0] == 0 ||
	    d_namecmp(name, ".") == 0 ||
	    d_namecmp(name, "..") == 0) {
		fs_iput(dp);
		fs_iput(ip);
		return -1;
	}
	/* 跨文件系统：i_op 不同则拒绝（类 EXDEV） */
	if (!dp->i_op || !dp->i_op->link || dp->i_op != ip->i_op) {
		fs_iput(dp);
		fs_iput(ip);
		return -1;
	}
	if (dp->i_op->link(dp, name, ip) < 0) {
		fs_iput(dp);
		fs_iput(ip);
		return -1;
	}
	fs_iput(dp);
	fs_iput(ip);
	return 0;
}

/* symlink(target, linkpath)：创建符号链接 */
int fs_symlink(const char *target, const char *linkpath)
{
	char name[DIRSIZ];
	struct inode *dp;

	if (!target || !linkpath || !linkpath[0])
		return -1;

	dp = namex(linkpath, 1, name, 1);
	if (!dp)
		return -1;
	if (name[0] == 0 ||
	    d_namecmp(name, ".") == 0 ||
	    d_namecmp(name, "..") == 0) {
		fs_iput(dp);
		return -1;
	}
	if (!dp->i_op || !dp->i_op->symlink) {
		fs_iput(dp);
		return -1;
	}
	if (dp->i_op->symlink(dp, name, target) < 0) {
		fs_iput(dp);
		return -1;
	}
	fs_iput(dp);
	return 0;
}

/* unlink(path)：删除非目录目录项 */
int fs_unlink(const char *path)
{
	char name[DIRSIZ];
	struct inode *dp, *ip;

	if (!path || !path[0])
		return -1;

	dp = namex(path, 1, name, 1);
	if (!dp)
		return -1;
	if (name[0] == 0 ||
	    d_namecmp(name, ".") == 0 ||
	    d_namecmp(name, "..") == 0) {
		fs_iput(dp);
		return -1;
	}
	if (!dp->i_op || !dp->i_op->lookup || !dp->i_op->unlink) {
		fs_iput(dp);
		return -1;
	}

	ip = dp->i_op->lookup(dp, name);
	if (!ip) {
		fs_iput(dp);
		return -1;
	}
	if (ip->type == T_DIR) {
		fs_iput(ip);
		fs_iput(dp);
		return -1;
	}
	fs_iput(ip);

	if (dp->i_op->unlink(dp, name) < 0) {
		fs_iput(dp);
		return -1;
	}
	fs_iput(dp);
	return 0;
}

/* rename(old, new)：同文件系统内重命名或移动 */
int fs_rename(const char *oldpath, const char *newpath)
{
	char oldname[DIRSIZ], newname[DIRSIZ];
	struct inode *old_dir, *new_dir;

	if (!oldpath || !oldpath[0] || !newpath || !newpath[0])
		return -1;

	old_dir = namex(oldpath, 1, oldname, 1);
	if (!old_dir)
		return -1;
	if (oldname[0] == 0 ||
	    d_namecmp(oldname, ".") == 0 ||
	    d_namecmp(oldname, "..") == 0) {
		fs_iput(old_dir);
		return -1;
	}

	new_dir = namex(newpath, 1, newname, 1);
	if (!new_dir) {
		fs_iput(old_dir);
		return -1;
	}
	if (newname[0] == 0 ||
	    d_namecmp(newname, ".") == 0 ||
	    d_namecmp(newname, "..") == 0) {
		fs_iput(new_dir);
		fs_iput(old_dir);
		return -1;
	}

	if (!old_dir->i_op || !old_dir->i_op->rename ||
	    old_dir->i_op != new_dir->i_op) {
		fs_iput(new_dir);
		fs_iput(old_dir);
		return -1;
	}
	if (old_dir->i_op->rename(old_dir, oldname, new_dir, newname) < 0) {
		fs_iput(new_dir);
		fs_iput(old_dir);
		return -1;
	}
	fs_iput(new_dir);
	fs_iput(old_dir);
	return 0;
}
