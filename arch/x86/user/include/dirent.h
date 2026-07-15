/*
 * 目录项（用户 ABI），字段命名对齐 Linux linux_dirent64 / dirent。
 * 读目录 fd 时按本结构顺序返回（教学实现用定长记录）。
 */
#ifndef __USER_DIRENT_H__
#define __USER_DIRENT_H__

/* 与 Linux <dirent.h> 一致的 d_type 值 */
#define DT_UNKNOWN	0
#define DT_FIFO		1
#define DT_CHR		2
#define DT_DIR		4
#define DT_BLK		6
#define DT_REG		8
#define DT_LNK		10
#define DT_SOCK		12

/* 本内核路径分量上限（ramfs）；Linux NAME_MAX 多为 255 */
#define NAME_MAX	27

struct dirent {
	unsigned int	d_ino;		/* inode 号 */
	unsigned int	d_off;		/* 下一记录在目录流中的偏移 */
	unsigned short	d_reclen;	/* 本记录长度 */
	unsigned char	d_type;		/* DT_* */
	char		d_name[NAME_MAX + 1];
} __attribute__((packed));

#endif
