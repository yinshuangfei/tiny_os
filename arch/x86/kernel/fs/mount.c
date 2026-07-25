/*
 * 挂载编排（对齐 Linux fs/namespace.c 教学简化）。
 *
 * 流程：注册驱动 → 读盘上魔数识别 FS → 再 fill_super 按需加载 → 挂接。
 * 具体文件系统不在 main 中出现，由本文件 mount_init 统一拉起。
 */
#include "../types.h"
#include "../defs.h"
#include "../param.h"
#include "../proc.h"
#include "../printk.h"
#include "../block/blk.h"
#include "vfs.h"
#include "mount.h"
#include "ext2.h"

#define NMOUNT	256

struct vfsmount {
	int used;			/* 是否被使用 */
	struct inode *mnt_root;		/* 被挂载的 FS 根（持引用） */
	struct inode *mnt_parent;	/* 覆盖层父目录（持引用，类 Linux mnt_parent） */
	char mnt_name[DIRSIZ];		/* 在父目录中的名（类 Linux mnt_name） */
	char mnt_dev[32];		/* 设备名，如 /dev/hda（供 /proc/mounts） */
	char mnt_dir[NNAME];		/* 挂载点绝对路径 */
	struct file_system_type *type;	/* 文件系统类型 */
};

/* 全局已注册的文件系统类型链表 */
static struct file_system_type *file_systems;
static struct vfsmount mounts[NMOUNT];

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

static void mnt_namecpy(char *dst, const char *src)
{
	int i;

	for (i = 0; i < DIRSIZ - 1 && src[i]; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

static void mnt_strcpy(char *dst, int max, const char *src)
{
	int i;

	if (!dst || max <= 0)
		return;
	for (i = 0; i < max - 1 && src && src[i]; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

/* 规范化挂载点为绝对路径（"mnt" → "/mnt"） */
static void mnt_norm_dir(const char *dir_name, char *out, int max)
{
	int i = 0;

	if (!out || max <= 0)
		return;
	if (!dir_name || !dir_name[0]) {
		out[0] = '\0';
		return;
	}
	if (dir_name[0] != '/') {
		if (i < max - 1)
			out[i++] = '/';
	}
	while (*dir_name && i < max - 1)
		out[i++] = *dir_name++;
	out[i] = '\0';
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

static struct file_system_type *find_fs_type(const char *name)
{
	struct file_system_type *p;

	if (!name || !name[0])
		return 0;
	for (p = file_systems; p; p = p->next) {
		if (fs_name_eq(p->name, name))
			return p;
	}
	return 0;
}

/* 去掉 "/dev/" 前缀，便于 mount("/dev/hda", ...) */
static const char *mount_dev_name(const char *name)
{
	if (!name || !name[0])
		return 0;
	if (name[0] == '/' && name[1] == 'd' && name[2] == 'e' &&
	    name[3] == 'v' && name[4] == '/')
		return name[5] ? name + 5 : 0;
	return name;
}

/* 获取挂载点的叶子名称（仅根下单层，如 "mnt" / "/mnt"） */
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

static struct vfsmount *mnt_find_root(struct inode *root)
{
	int i;

	if (!root)
		return 0;
	for (i = 0; i < NMOUNT; i++) {
		if (mounts[i].used && mounts[i].mnt_root == root)
			return &mounts[i];
	}
	return 0;
}

static int mnt_add(struct inode *parent, const char *name, struct inode *root,
		   struct file_system_type *type, const char *dev,
		   const char *dirpath)
{
	int i;

	for (i = 0; i < NMOUNT; i++) {
		if (mounts[i].used)
			continue;
		mounts[i].used = 1;
		mounts[i].mnt_parent = fs_idup(parent);
		mounts[i].mnt_root = fs_idup(root);
		mnt_namecpy(mounts[i].mnt_name, name);
		mnt_strcpy(mounts[i].mnt_dev, sizeof(mounts[i].mnt_dev),
			   dev ? dev : "none");
		mnt_strcpy(mounts[i].mnt_dir, sizeof(mounts[i].mnt_dir),
			   dirpath ? dirpath : "/");
		mounts[i].type = type;
		return 0;
	}
	return -1;
}

static void mnt_del(struct vfsmount *m)
{
	if (!m || !m->used)
		return;
	if (m->type && m->type->kill_sb)
		m->type->kill_sb();
	fs_iput(m->mnt_root);
	fs_iput(m->mnt_parent);
	m->mnt_root = 0;
	m->mnt_parent = 0;
	m->type = 0;
	m->mnt_name[0] = '\0';
	m->mnt_dev[0] = '\0';
	m->mnt_dir[0] = '\0';
	m->used = 0;
}

/* ip 是否位于 mnt_root 之下（含自身） */
static int inode_on_mount(struct inode *ip, struct inode *mnt_root)
{
	struct inode *p;

	for (p = ip; p; p = p->parent) {
		if (p == mnt_root)
			return 1;
		if (p == vfs_root() || !p->parent || p->parent == p)
			break;
	}
	return 0;
}

/* 有进程 cwd 或打开文件仍在该挂载上则忙 */
static int mount_busy(struct inode *mnt_root)
{
	int i, fd;
	struct proc *p;
	struct file *f;

	if (!mnt_root || !proc_table)
		return 0;
	for (i = 0; i < NPROC; i++) {
		p = &proc_table[i];
		if (p->state == UNUSED)
			continue;
		if (p->cwd && inode_on_mount(p->cwd, mnt_root))
			return 1;
		for (fd = 0; fd < NOFILE; fd++) {
			f = p->ofile[fd];
			if (f && f->ip && inode_on_mount(f->ip, mnt_root))
				return 1;
		}
	}
	return 0;
}

/* 将 FS 根挂到 parent 下名为 leaf 的目录项 */
static int mount_link_dir(struct inode *parent, const char *leaf,
			  struct inode *child, struct file_system_type *type,
			  const char *dev, const char *dirpath)
{
	if (!parent || !child || !leaf || !leaf[0])
		return -1;
	if (ramfs_link(parent, leaf, child) < 0)
		return -1;
	child->parent = parent;
	if (mnt_add(parent, leaf, child, type, dev, dirpath) < 0) {
		ramfs_detach(parent, leaf);
		return -1;
	}
	fs_iput(child);	/* 去掉 fill_super；目录项 + 挂载表持引用 */
	return 0;
}

/*
 * 挂接挂载点：
 * - 路径已存在：须为空目录，先 rmdir 再挂上 FS 根；
 * - 路径不存在：仅允许根下单层名（与启动时 /mnt 一致）。
 */
static int mount_attach(const char *dir_name, struct inode *child,
			struct file_system_type *type, const char *dev)
{
	struct inode *mp, *parent;
	char leaf[DIRSIZ];
	char dirpath[NNAME];
	const char *leafp;
	int i;

	mnt_norm_dir(dir_name, dirpath, sizeof(dirpath));

	mp = fs_namei(dir_name);
	if (mp) {
		if (mp->type != T_DIR || mp->dents) {
			fs_iput(mp);
			return -1;
		}
		parent = mp->parent;
		if (!parent) {
			fs_iput(mp);
			return -1;
		}
		for (i = 0; i < DIRSIZ - 1 && mp->name[i]; i++)
			leaf[i] = mp->name[i];
		leaf[i] = '\0';
		if (!leaf[0]) {
			fs_iput(mp);
			return -1;
		}
		parent = fs_idup(parent);
		fs_iput(mp);
		if (fs_rmdir(dir_name) < 0) {
			fs_iput(parent);
			return -1;
		}
		if (mount_link_dir(parent, leaf, child, type, dev, dirpath) < 0) {
			fs_iput(parent);
			return -1;
		}
		fs_iput(parent);
		return 0;
	}

	leafp = mount_leaf_name(dir_name);
	if (!leafp)
		return -1;
	return mount_link_dir(vfs_root(), leafp, child, type, dev, dirpath);
}

int do_mount(const char *dev_name, const char *dir_name, const char *fstype)
{
	struct file_system_type *type;
	struct gendisk *gd;
	struct inode *mnt;
	const char *disk;
	char devpath[40];

	disk = mount_dev_name(dev_name);
	if (!disk || !dir_name || !dir_name[0]) {
		printk(KERN_WARNING "mount: bad args\n");
		return -1;
	}

	gd = blk_lookup_name(disk);
	if (!gd) {
		printk(KERN_WARNING "mount: disk %s not found\n", disk);
		return -1;
	}

	if (fstype && fstype[0]) {
		type = find_fs_type(fstype);
		if (!type) {
			printk(KERN_WARNING "mount: unknown fstype %s\n", fstype);
			return -1;
		}
	} else {
		/* 1) 读魔数识别 */
		type = identify_fs(gd);
		if (!type) {
			printk(KERN_WARNING "mount: no filesystem recognized on %s\n",
			       disk);
			return -1;
		}
	}
	printk(KERN_INFO "mount: %s is %s\n", disk, type->name);

	/* 2) 按需加载该 FS */
	mnt = type->fill_super(gd);
	if (!mnt) {
		printk(KERN_WARNING "mount: %s fill_super failed on %s\n",
		       type->name, disk);
		return -1;
	}

	snprintf(devpath, sizeof(devpath), "/dev/%s", disk);
	if (mount_attach(dir_name, mnt, type, devpath) < 0) {
		printk(KERN_WARNING "mount: attach %s failed\n", dir_name);
		fs_iput(mnt);
		return -1;
	}

	printk(KERN_INFO "mount: %s on %s type %s\n",
	       disk, dir_name, type->name);
	return 0;
}

/*
 * /proc/mounts 内容（对齐 Linux：device dir type opts dump pass）。
 * 含根 ramfs、/proc，以及块设备挂载表项。
 */
int mounts_proc_show(char *buf, uint size)
{
	uint len = 0;
	int i;

	if (!buf || !size)
		return -1;

	len += (uint)snprintf(buf + len, size - len,
			      "none / ramfs rw 0 0\n");
	if (len >= size - 1)
		return (int)len;
	len += (uint)snprintf(buf + len, size - len,
			      "none /proc proc rw 0 0\n");

	for (i = 0; i < NMOUNT; i++) {
		const char *typen;

		if (!mounts[i].used)
			continue;
		if (len >= size - 1)
			break;
		typen = (mounts[i].type && mounts[i].type->name)
			? mounts[i].type->name : "unknown";
		len += (uint)snprintf(buf + len, size - len,
				      "%s %s %s rw 0 0\n",
				      mounts[i].mnt_dev[0] ? mounts[i].mnt_dev
							   : "none",
				      mounts[i].mnt_dir[0] ? mounts[i].mnt_dir
							   : "/",
				      typen);
	}
	return (int)len;
}

int do_umount(const char *dir_name)
{
	struct inode *mp, *parent;
	struct vfsmount *m;
	char leaf[DIRSIZ];

	if (!dir_name || !dir_name[0])
		return -1;

	mp = fs_namei(dir_name);
	if (!mp)
		return -1;

	m = mnt_find_root(mp);
	if (!m) {
		fs_iput(mp);
		return -1;
	}

	if (mount_busy(mp)) {
		printk(KERN_WARNING "umount: %s busy\n", dir_name);
		fs_iput(mp);
		return -1;
	}

	mnt_namecpy(leaf, m->mnt_name);
	parent = fs_idup(m->mnt_parent);

	if (ramfs_detach(parent, leaf) < 0) {
		fs_iput(parent);
		fs_iput(mp);
		return -1;
	}
	fs_iput(mp);	/* namei 引用；目录项引用已在 detach 中放下 */

	mnt_del(m);	/* kill_sb + 放下挂载表引用 */

	/* 恢复空的挂载点目录（对齐“卸下后露出原目录”） */
	if (parent->i_op && parent->i_op->mkdir)
		(void)parent->i_op->mkdir(parent, leaf);
	fs_iput(parent);

	printk(KERN_INFO "umount: %s\n", dir_name);
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
		if (do_mount(mount_disks[i], "mnt", 0) == 0)
			return;
	}
	printk(KERN_INFO "mount: no mountable filesystem found\n");
}
