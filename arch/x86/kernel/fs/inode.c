/*
 * VFS：inode 引用计数与通用读写入口（对齐 Linux fs/inode.c 角色）。
 * 回收细节由 ip->i_op->evict 交给具体文件系统。
 */
#include "../types.h"
#include "../defs.h"
#include "vfs.h"

/* 根 inode */
static struct inode *vfs_root_inode;

/* 设置根 inode */
void vfs_set_root(struct inode *root)
{
	vfs_root_inode = root;
}

/* 获取根 inode */
struct inode *vfs_root(void)
{
	return vfs_root_inode;
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
	if (ip->ref == 0 && ip != vfs_root_inode) {
		if (ip->i_op && ip->i_op->evict)
			ip->i_op->evict(ip);
	}
}

/* 读取 inode 数据 */
int fs_readi(struct inode *ip, char *dst, uint off, uint n)
{
	if (!ip || !ip->i_op || !ip->i_op->read)
		return -1;
	return ip->i_op->read(ip, dst, off, n);
}

/* 写入 inode 数据 */
int fs_writei(struct inode *ip, char *src, uint off, uint n)
{
	if (!ip || !ip->i_op || !ip->i_op->write)
		return -1;
	return ip->i_op->write(ip, src, off, n);
}
