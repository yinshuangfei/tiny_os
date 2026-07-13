#ifndef __FS_H__
#define __FS_H__

#include "../types.h"
#include "../param.h"

#define DIRSIZ		28		/* 目录项大小 */
#define NINODE		64		/* 最大inode数 */
#define NFILE		64		/* 最大文件数 */

/* inode 类型 */
enum inode_type {
	T_DIR	= 1,			/* 目录 */
	T_FILE	= 2,			/* 文件 */
	T_DEV	= 3,			/* 设备 */
};

/* 控制台设备号（/dev/console） */
#define DEV_CONSOLE	1

struct proc;
struct inode;

struct dirent {
	char name[DIRSIZ];		/* 目录项名称 */
	struct inode *ip;		/* 指向的inode */
	struct dirent *next;		/* 下一个目录项 */
};

struct inode {
	short type;			/* 文件类型 */
	short major;			/* T_DEV */
	uint ref;			/* 引用计数 */
	uint size;			/* 文件大小 */
	char *data;			/* T_FILE 内容 */
	struct dirent *dents;		/* T_DIR 子项 */
};

struct file {
	/* 文件类型 */
	enum {
		FD_NONE, 		/* 无文件 */
		FD_INODE, 		/* 普通文件 */
		FD_DEVICE 		/* 设备文件 */
	} type;
	int ref;			/* 引用计数 */
	char readable;			/* 可读标志 */
	char writable;			/* 可写标志 */
	struct inode *ip;		/* 指向的inode */
	uint off;			/* 文件偏移量 */
	short major;			/* 主设备号 */
};

/* open flags（与常见 Unix 子集对齐） */
#define O_RDONLY	0x000
#define O_WRONLY	0x001
#define O_RDWR		0x002
#define O_CREATE	0x200

void fs_init(void);

struct inode *fs_namei(const char *path);
struct inode *fs_create(const char *path, short type);
void fs_iput(struct inode *ip);
struct inode *fs_idup(struct inode *ip);

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
