#ifndef __FS_H__
#define __FS_H__

#include "../types.h"
#include "../param.h"
#include "../major.h"
#include "../../user/include/dirent.h"

#define DIRSIZ		(NAME_MAX + 1)	/* 目录项名缓冲，与 NAME_MAX 对齐 */
#define NINODE		64		/* 最大inode数 */
#define NFILE		64		/* 最大文件数 */

/*
 * inode 类型（内核内部）。
 * 字符/块设备对齐 Linux S_IFCHR / S_IFBLK；设备号存在 inode 上（类 i_rdev）。
 * 主设备号见 major.h（CONSOLE_MAJOR / HD_MAJOR 等）。
 */
enum inode_type {
	T_DIR	= 1,			/* 目录 */
	T_FILE	= 2,			/* 普通文件 */
	T_CHAR	= 3,			/* 字符设备（如 console） */
	T_BLK	= 4,			/* 块设备（如 hda） */
};

struct proc;
struct inode;

/* 内核目录树节点（对应 Linux VFS 的 dentry 角色，非用户 ABI） */
struct dentry {
	char name[DIRSIZ];		/* 目录项名称 */
	struct inode *ip;		/* 指向的 inode */
	struct dentry *next;		/* 下一个目录项 */
};

struct inode {
	uint inum;			/* inode 号（1..NINODE） */
	short type;			/* 文件类型 */
	dev_t rdev;			/* T_CHAR / T_BLK：设备号（Linux i_rdev，12+20） */
	uint ref;			/* 引用计数 */
	uint size;			/* 文件大小（块设备可为容量字节数） */
	char *data;			/* T_FILE 内容 */
	struct dentry *dents;		/* T_DIR 子项 */
	struct inode *parent;		/* 父目录（root->parent == root） */
};

/*
 * 打开文件（对齐 Linux struct file 教学子集）。
 * 设备号不缓存在 file 上，经 f_inode（此处 ip）读取，同 Linux。
 */
struct file {
	/* 文件类型 */
	enum {
		FD_NONE,		/* 无文件 */
		FD_INODE,		/* 普通文件 / 目录 */
		FD_CHAR,		/* 字符设备 */
		FD_BLOCK		/* 块设备 */
	} type;
	int ref;			/* 引用计数 */
	char readable;			/* 可读标志 */
	char writable;			/* 可写标志 */
	struct inode *ip;		/* 指向的 inode（Linux f_inode） */
	uint off;			/* 文件偏移量（Linux f_pos） */
};

/* open flags（与常见 Unix 子集对齐） */
#define O_RDONLY	0x000
#define O_WRONLY	0x001
#define O_RDWR		0x002
#define O_CREATE	0x200

void fs_init(void);

struct inode *fs_namei(const char *path);
struct inode *fs_create(const char *path, short type);
/* 创建设备节点（T_CHAR / T_BLK）；成功返回持有引用的 inode */
struct inode *fs_mknod(const char *path, short type,
		       unsigned int major, unsigned int minor);
int fs_mkdir(const char *path);
int fs_rmdir(const char *path);
void fs_iput(struct inode *ip);
struct inode *fs_idup(struct inode *ip);
int fs_getcwd(char *buf, int max);

int fs_readi(struct inode *ip, char *dst, uint off, uint n);
int fs_writei(struct inode *ip, char *src, uint off, uint n);

void fileinit(void);
struct file *filealloc(void);
struct file *filedup(struct file *f);
void fileclose(struct file *f);
int fileread(struct file *f, char *dst, int n);
int filewrite(struct file *f, char *src, int n);

int fdalloc(struct file *f);
struct file *fdget(int fd);
void fd_install_stdio(struct proc *p);
void fd_copy(struct proc *dst, struct proc *src);
void fd_closeall(struct proc *p);

#endif
