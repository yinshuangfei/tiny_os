/*
 * dentry 缓存（对齐 Linux fs/dcache.c 教学子集）。
 * 本内核用 inode->dents 链表挂接目录项；供 ramfs / proc 等后端共用。
 */
#ifndef __DCACHE_H__
#define __DCACHE_H__

#include "../types.h"
#include "../param.h"
#include "../../user/include/dirent.h"

#define DIRSIZ		(NAME_MAX + 1)	/* 目录项名缓冲，与 NAME_MAX 对齐 */

struct inode;

/* 内核目录树节点（对应 Linux VFS 的 dentry 角色，非用户 ABI） */
struct dentry {
	char name[DIRSIZ];		/* 目录项名称 */
	struct inode *ip;		/* 指向的 inode */
	struct dentry *next;		/* 下一个目录项 */
};

int d_namecmp(const char *a, const char *b);
void d_namecpy(char *dst, const char *src);
struct dentry *d_lookup(struct inode *dir, const char *name);
int d_add(struct inode *dir, const char *name, struct inode *ip);
int d_unlink(struct inode *dir, const char *name);

#endif
