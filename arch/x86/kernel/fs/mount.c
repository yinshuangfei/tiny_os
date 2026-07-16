/*
 * 挂载编排（对齐 Linux fs/namespace.c 教学简化）。
 *
 * 流程：注册驱动 → 读盘上魔数识别 FS → 再 fill_super 按需加载 → 挂接。
 * 具体文件系统不在 main 中出现，由本文件 mount_init 统一拉起。
 */
#include "../types.h"
#include "../defs.h"
#include "../printk.h"
#include "../block/blk.h"
#include "vfs.h"
#include "mount.h"
#include "ext2.h"

/* 全局已注册的文件系统类型链表 */
static struct file_system_type *file_systems;

/* 可挂载的块设备名（与 ide 注册名一致） */
static const char *mount_disks[] = {
	"hda", "hdb", "hdc", "hdd", "hde", "hdf", 0
};

static int fs_name_eq(const char *a, const char *b)
{
	if (!a || !b)
		return 0;
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

/* 注册文件系统 */
int register_filesystem(struct file_system_type *fs)
{
	struct file_system_type *p;

	if (!fs || !fs->name || !fs->fill_super)
		return -1;
	for (p = file_systems; p; p = p->next) {
		if (fs_name_eq(p->name, fs->name)) {
			printk(KERN_WARNING "mount: fs '%s' already registered\n",
			       fs->name);
			return -1;
		}
	}
	fs->next = file_systems;
	file_systems = fs;
	return 0;
}

/* 从块设备任意字节偏移读取 16 位小端值（用于魔数） */
static int bread_u16(struct gendisk *gd, uint off, ushort *out)
{
	char sect[BLOCK_SECTOR_SIZE];
	sector_t sec;
	uint within;

	if (!gd || !out)
		return -1;
	sec = off / BLOCK_SECTOR_SIZE;
	within = off % BLOCK_SECTOR_SIZE;
	if (within + 2 > BLOCK_SECTOR_SIZE)
		return -1;
	if (blkdev_read_sect(gd, sec, sect) < 0)
		return -1;
	*out = (ushort)((uchar)sect[within] | ((ushort)(uchar)sect[within + 1] << 8));
	return 0;
}

/* 读超级块位置魔数，匹配已注册类型（尚未 fill_super） */
static struct file_system_type *identify_fs(struct gendisk *gd)
{
	struct file_system_type *p;
	ushort magic;

	for (p = file_systems; p; p = p->next) {
		if (bread_u16(gd, p->magic_off, &magic) < 0)
			continue;
		if (magic == p->magic)
			return p;
	}
	return 0;
}

/* 获取挂载点的叶子名称 */
static const char *mount_leaf_name(const char *name)
{
	const char *leaf;
	const char *p;

	if (!name || !name[0])
		return 0;
	leaf = name;
	if (leaf[0] == '/')
		leaf++;
	if (!leaf[0])
		return 0;
	for (p = leaf; *p; p++) {
		if (*p == '/')
			return 0;
	}
	return leaf;
}

/* 目录 leaf 为 name，inode 为 child 的目录项挂载根节点 */
static int mount_link_root(const char *leaf, struct inode *child)
{
	struct inode *root;

	root = vfs_root();
	if (!root || !child || !leaf)
		return -1;

	/* TODO: 这里有点强制挂载 */
	if (ramfs_link(root, leaf, child) < 0)
		return -1;
	child->parent = root;
	fs_iput(child);
	return 0;
}

int do_mount(const char *dev_name, const char *dir_name)
{
	struct file_system_type *type;
	struct gendisk *gd;
	struct inode *mnt;
	const char *leaf;

	leaf = mount_leaf_name(dir_name);
	if (!dev_name || !leaf) {
		printk(KERN_WARNING "mount: bad args\n");
		return -1;
	}

	gd = blk_lookup_name(dev_name);
	if (!gd) {
		printk(KERN_WARNING "mount: disk %s not found\n", dev_name);
		return -1;
	}

	/* 1) 读魔数识别 */
	type = identify_fs(gd);
	if (!type) {
		printk(KERN_WARNING "mount: no filesystem recognized on %s\n",
		       dev_name);
		return -1;
	}
	printk(KERN_INFO "mount: %s is %s\n", dev_name, type->name);

	/* 2) 按需加载该 FS */
	mnt = type->fill_super(gd);
	if (!mnt) {
		printk(KERN_WARNING "mount: %s fill_super failed on %s\n",
		       type->name, dev_name);
		return -1;
	}

	if (mount_link_root(leaf, mnt) < 0) {
		printk(KERN_WARNING "mount: link /%s failed\n", leaf);
		fs_iput(mnt);
		return -1;
	}

	printk(KERN_INFO "mount: %s on /%s type %s\n",
	       dev_name, leaf, type->name);
	return 0;
}

/* 扫描块设备，首个识别成功的挂到 /mnt */
void mount_init(void)
{
	int i;

	/* 注册驱动（仅登记 probe 用魔数与 fill_super；真正加载在识别之后） */
	ext2_init();

	/* 扫描块设备，首个识别成功的挂到 /mnt */
	for (i = 0; mount_disks[i]; i++) {
		if (!blk_lookup_name(mount_disks[i]))
			continue;
		if (do_mount(mount_disks[i], "mnt") == 0)
			return;
	}
	printk(KERN_INFO "mount: no mountable filesystem found\n");
}
