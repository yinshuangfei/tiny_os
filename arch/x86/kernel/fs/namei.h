/*
 * 路径解析与目录操作（对齐 Linux fs/namei.c 教学子集）。
 */
#ifndef __NAMEI_H__
#define __NAMEI_H__

struct inode;

struct inode *fs_namei(const char *path);
struct inode *fs_namei_nofollow(const char *path);
struct inode *fs_create(const char *path, short type);
struct inode *fs_mknod(const char *path, short type,
		       unsigned int major, unsigned int minor);
int fs_mkdir(const char *path);
int fs_rmdir(const char *path);
int fs_link(const char *oldpath, const char *newpath);
int fs_symlink(const char *target, const char *linkpath);
int fs_unlink(const char *path);
int fs_rename(const char *oldpath, const char *newpath);
int fs_getcwd(char *buf, int max);

#endif
