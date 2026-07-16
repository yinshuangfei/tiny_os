/*
 * VFS 挂载：类型注册、魔数识别、按需 fill_super（教学子集）。
 */
#ifndef __MOUNT_H__
#define __MOUNT_H__

struct gendisk;
struct inode;

/*
 * 一种文件系统类型。
 * magic_off / magic：挂载时读盘识别；命中后再调 fill_super 加载。
 */
struct file_system_type {
	const char *name;		/* 文件系统名称 */
	uint magic_off;			/* 魔数在设备上的字节偏移 */
	ushort magic;			/* 小端魔数，如 ext2 的 0xEF53 */
	struct inode *(*fill_super)(struct gendisk *gd);
	struct file_system_type *next;
};

int register_filesystem(struct file_system_type *fs);

/* 识别并挂载单个块设备到 dir_name（如 "mnt"） */
int do_mount(const char *dev_name, const char *dir_name);

/*
 * 启动时挂载入口：注册各 FS 驱动，扫描块设备，
 * 读超级块魔数识别后按需 fill_super，首个成功者挂到 /mnt。
 */
void mount_init(void);

#endif
