/*
 * VFS：inode 引用计数与通用读写 / 截断入口（对齐 Linux fs/inode.c 角色）。
 * 回收细节由 ip->i_op->evict 交给具体文件系统。
 */
#include "../types.h"
#include "../defs.h"
#include "../timer.h"
#include "vfs.h"

static uint vfs_now_sec(void)
{
	return timer_ticks() / TIMER_HZ;
}

/* 新建 inode 时填充默认元数据（mode / nlink / 时间戳等） */
void fs_inode_init_meta(struct inode *ip, short type, uint mode)
{
	if (!ip)
		return;
	ip->type = type;
	ip->mode = mode & 07777;
	ip->nlink = (type == T_DIR) ? 2 : 1;	/* 目录含 . */
	ip->uid = 0;
	ip->gid = 0;
	ip->atime = ip->mtime = ip->ctime = vfs_now_sec();
}

/* 更新 atime / mtime（写时顺带更新 ctime） */
void fs_inode_touch(struct inode *ip, int set_mtime, int set_atime)
{
	uint now;

	if (!ip)
		return;
	now = vfs_now_sec();
	if (set_atime)
		ip->atime = now;
	if (set_mtime) {
		ip->mtime = now;
		ip->ctime = now;
	}
}

/* 增加 inode 引用计数 */
struct inode *fs_idup(struct inode *ip)
{
	if (ip)
		ip->ref++;
	return ip;
}

/* 减少 inode 引用计数 */
void fs_iput(struct inode *ip)
{
	if (!ip)
		return;
	if (ip->ref <= 0)
		panic("fs_iput");

	ip->ref--;
	/*
	 * 根永不回收。其余 ref==0 时调后端 evict（如 ramfs 清 data/dents）。
	 */
	if (ip->ref == 0 && ip != vfs_root()) {
		if (ip->i_op && ip->i_op->evict)
			ip->i_op->evict(ip);
	}
}

/* 读取 inode 数据 */
int fs_readi(struct inode *ip, char *dst, uint off, uint n)
{
	int r;

	if (!ip || !ip->i_op || !ip->i_op->read)
		return -1;
	r = ip->i_op->read(ip, dst, off, n);
	if (r > 0)
		fs_inode_touch(ip, 0, 1);
	return r;
}

/* 写入 inode 数据 */
int fs_writei(struct inode *ip, char *src, uint off, uint n)
{
	int r;

	if (!ip || !ip->i_op || !ip->i_op->write)
		return -1;
	r = ip->i_op->write(ip, src, off, n);
	if (r > 0)
		fs_inode_touch(ip, 1, 0);
	return r;
}

/* 截断 / 扩展普通文件长度（经 i_op->truncate） */
int fs_truncate(struct inode *ip, uint length)
{
	int r;

	if (!ip || ip->type != T_FILE)
		return -1;
	if (!ip->i_op || !ip->i_op->truncate)
		return -1;
	r = ip->i_op->truncate(ip, length);
	if (r == 0)
		fs_inode_touch(ip, 1, 0);
	return r;
}
