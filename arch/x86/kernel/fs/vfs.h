/*
 * VFS（Virtual File System）抽象层头文件。
 * 对齐 Linux include/linux/fs.h 教学子集：inode / dentry / file 与通用 API。
 * 实现见 inode.c、namei.c、file.c；具体存储由 inode_operations 后端提供。
 */
#ifndef __VFS_H__
#define __VFS_H__

#include "../types.h"
#include "../param.h"
#include "../major.h"
#include "../../user/include/dirent.h"

#define DIRSIZ		(NAME_MAX + 1)	/* 目录项名缓冲，与 NAME_MAX 对齐 */
#define NINODE		64		/* 最大 inode 数（各后端可自用） */
#define NFILE		64		/* 最大打开文件数 */

/*
 * inode 类型（内核内部）。
 * 字符/块设备对齐 Linux S_IFCHR / S_IFBLK；设备号在 rdev（类 i_rdev）。
 * 主设备号见 major.h（CONSOLE_MAJOR / HD_MAJOR 等）。
 */
enum inode_type {
	T_DIR	= 1,			/* 目录 */
	T_FILE	= 2,			/* 普通文件 */
	T_CHAR	= 3,			/* 字符设备（如 console） */
	T_BLK	= 4,			/* 块设备（如 hda） */
	T_FIFO	= 5,			/* 管道 / FIFO（S_IFIFO） */
};

struct proc;
struct inode;
struct pipe;
struct file;

/* 内核目录树节点（对应 Linux VFS 的 dentry 角色，非用户 ABI） */
struct dentry {
	char name[DIRSIZ];		/* 目录项名称 */
	struct inode *ip;		/* 指向的 inode */
	struct dentry *next;		/* 下一个目录项 */
};

/*
 * 目录/文件后端操作（对齐 Linux inode_operations 教学子集）。
 * lookup 成功时返回已 fs_idup 的 inode；失败返回 NULL。
 */
struct inode_operations {
	struct inode *(*lookup)(struct inode *dir, const char *name);
	int (*create)(struct inode *dir, const char *name, short type,
		      struct inode **out);
	int (*mkdir)(struct inode *dir, const char *name);
	int (*rmdir)(struct inode *dir, const char *name);
	int (*mknod)(struct inode *dir, const char *name, short type, dev_t rdev);
	/* 硬链接：在 dir 下新增 name → ip（不可链接目录） */
	int (*link)(struct inode *dir, const char *name, struct inode *ip);
	/* 删除目录项（不可删目录，目录用 rmdir） */
	int (*unlink)(struct inode *dir, const char *name);
	/* 同文件系统内重命名/移动 */
	int (*rename)(struct inode *old_dir, const char *old_name,
		      struct inode *new_dir, const char *new_name);
	int (*get_name)(struct inode *dir, struct inode *child, char *name);
	void (*evict)(struct inode *ip);
	int (*read)(struct inode *ip, char *dst, uint off, uint n);
	int (*write)(struct inode *ip, char *src, uint off, uint n);
};

struct inode {
	uint inum;			/* inode 号 */
	short type;			/* 文件类型 */
	dev_t rdev;			/* T_CHAR / T_BLK：设备号（12+20） */
	uint ref;			/* 引用计数 */
	uint size;			/* 文件、目录大小（块设备可为容量字节数） */
	char *data;			/* T_FILE 内容（后端私有用法） */
	struct pipe *i_pipe;		/* T_FIFO：pipe_inode_info（类 Linux i_pipe） */
	struct dentry *dents;		/* T_DIR 子项（后端私有用法） */
	struct inode *parent;		/* 父目录（root->parent == root） */
	const struct inode_operations *i_op; /* 后端操作表 */
};

/*
 * 打开文件（对齐 Linux struct file 教学子集；属 VFS，见 file.c）。
 * 设备号不缓存在 file 上，经 f_inode（此处 ip）读取，同 Linux。
 * 匿名管道两端均为 FD_INODE，共享同一 T_FIFO inode（缓冲在 ip->i_pipe）。
 */
struct file {
	enum {
		FD_NONE,		/* 无文件 */
		FD_INODE,		/* 普通文件 / 目录 / FIFO */
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

/* lseek whence（与 Linux / 用户态 syscall.h 一致） */
#define SEEK_SET	0
#define SEEK_CUR	1
#define SEEK_END	2

/* VFS 根目录（由具体 FS 在 init 时 vfs_set_root） */
void vfs_set_root(struct inode *root);
struct inode *vfs_root(void);

/* 路径 / inode（对外仍用 fs_* 名） */
struct inode *fs_namei(const char *path);
struct inode *fs_create(const char *path, short type);
struct inode *fs_mknod(const char *path, short type,
		       unsigned int major, unsigned int minor);
int fs_mkdir(const char *path);
int fs_rmdir(const char *path);
int fs_link(const char *oldpath, const char *newpath);
int fs_unlink(const char *path);
int fs_rename(const char *oldpath, const char *newpath);
void fs_iput(struct inode *ip);
struct inode *fs_idup(struct inode *ip);
int fs_getcwd(char *buf, int max);
int fs_readi(struct inode *ip, char *dst, uint off, uint n);
int fs_writei(struct inode *ip, char *src, uint off, uint n);

/* 打开文件表 / 进程 fd（VFS，对齐 Linux fs/file.c 角色） */
void fileinit(void);
struct file *filealloc(void);
struct file *filedup(struct file *f);
void fileclose(struct file *f);
int fileread(struct file *f, char *dst, int n);
int filewrite(struct file *f, char *src, int n);
int filelseek(struct file *f, int offset, int whence);

int fdalloc(struct file *f);
struct file *fdget(int fd);
void fd_install_stdio(struct proc *p);
void fd_copy(struct proc *dst, struct proc *src);
void fd_closeall(struct proc *p);

/* 将 inode 链到 ramfs 目录（跨后端挂接，如 ext2 → /mnt） */
int ramfs_link(struct inode *dir, const char *name, struct inode *ip);

/* 匿名管道（Linux pipe(2) / pipefs 教学子集） */
int pipealloc(struct file **f0, struct file **f1);
void pipe_release(struct file *f);

#endif
