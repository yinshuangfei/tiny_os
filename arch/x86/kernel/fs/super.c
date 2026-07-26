/*
 * VFS 超级块 / 根挂载语境（对齐 Linux fs/super.c 教学子集）。
 * 本内核尚无完整 struct super_block，先集中管理 vfs 根 inode。
 */
#include "../types.h"
#include "../defs.h"
#include "vfs.h"

static struct inode *vfs_root_inode;

void vfs_set_root(struct inode *root)
{
	vfs_root_inode = root;
}

struct inode *vfs_root(void)
{
	return vfs_root_inode;
}
