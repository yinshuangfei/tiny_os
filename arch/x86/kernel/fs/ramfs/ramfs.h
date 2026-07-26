/*
 * ramfs 对外接口（对齐 Linux fs/ramfs；教学子集）。
 */
#ifndef __RAMFS_H__
#define __RAMFS_H__

struct inode;

/* 将 inode 链到 ramfs 目录（跨后端挂接，如 ext2 → /mnt） */
int ramfs_link(struct inode *dir, const char *name, struct inode *ip);
/* 强制摘除目录项（umount；不要求子目录为空） */
int ramfs_detach(struct inode *dir, const char *name);

#endif
