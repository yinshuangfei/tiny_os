/*
 * 只读 ext2 后端（教学子集）。
 * 经 inode_operations 接入 VFS；块 I/O 走 blkdev_read_sect。
 * 每挂载一份 ext2_sb_info，支持同类型多实例。
 *
 * 布局（block_size=1024，first_data_block=1）：
 *   0          引导区（未用）
 *   1          超级块
 *   2          块组描述符
 *   3          块位图
 *   4          inode 位图
 *   5..        inode 表
 *   随后       数据块（根目录、文件、子目录）
 */
#include "../../types.h"
#include "../../defs.h"
#include "../../param.h"
#include "../../printk.h"
#include "../../block/blk.h"
#include "../vfs.h"
#include "../mount.h"
#include "ext2.h"

#define EXT2_NICACHE		64		/* 全局 inode 缓存槽数（按 sbi+inum 区分） */
#define EXT2_NDIR_BLOCKS	12		/* 直接块数 */
#define EXT2_IND_BLOCK		12		/* 间接块数 */
#define EXT2_N_BLOCKS		15		/* 总块数 */

#define EXT2_S_IFMT		0xF000
#define EXT2_S_IFREG		0x8000
#define EXT2_S_IFDIR		0x4000

#define EXT2_FT_UNKNOWN		0
#define EXT2_FT_REG_FILE	1
#define EXT2_FT_DIR		2
#define EXT2_FT_CHRDEV		3
#define EXT2_FT_BLKDEV		4

#define EXT2_FEATURE_INCOMPAT_FILETYPE	0x2

/* 超级块 */
struct ext2_super_block {
	uint32	s_inodes_count;			/* inode 数 */
	uint32	s_blocks_count;			/* 块数 */
	uint32	s_r_blocks_count;		/* 只读块数 */
	uint32	s_free_blocks_count;		/* 空闲块数 */
	uint32	s_free_inodes_count;		/* 空闲 inode 数 */
	uint32	s_first_data_block;		/* 第一个数据块 */
	uint32	s_log_block_size;		/* 块大小对数 */
	uint32	s_log_frag_size;		/* 碎片大小对数 */
	uint32	s_blocks_per_group;		/* 组中块数 */
	uint32	s_frags_per_group;		/* 组中碎片数 */
	uint32	s_inodes_per_group;		/* 组中 inode 数 */
	uint32	s_mtime;			/* 修改时间 */
	uint32	s_wtime;			/* 写入时间 */
	uint16	s_mnt_count;			/* 挂载计数 */
	uint16	s_max_mnt_count;		/* 最大挂载计数 */
	uint16	s_magic;			/* 魔数 */
	uint16	s_state;			/* 状态 */
	uint16	s_errors;			/* 错误状态 */
	uint16	s_minor_rev_level;		/* 次要修订级别 */
	uint32	s_lastcheck;			/* 上次检查时间 */
	uint32	s_checkinterval;		/* 检查间隔 */
	uint32	s_creator_os;			/* 创建者操作系统 */
	uint32	s_rev_level;			/* 修订级别 */
	uint16	s_def_resuid;			/* 默认资源用户 ID */
	uint16	s_def_resgid;			/* 默认资源组 ID */
	uint32	s_first_ino;			/* 第一个 inode */
	uint16	s_inode_size;			/* inode 大小 */
	uint16	s_block_group_nr;		/* 块组号 */
	uint32	s_feature_compat;		/* 兼容特性 */
	uint32	s_feature_incompat;		/* 不兼容特性 */
	uint32	s_feature_ro_compat;		/* 只读兼容特性 */
} __attribute__((packed));

/* 组描述符 */
struct ext2_group_desc {
	uint32	bg_block_bitmap;		/* 块位图 */
	uint32	bg_inode_bitmap;		/* inode 位图 */
	uint32	bg_inode_table;			/* inode 表 */
	uint16	bg_free_blocks_count;		/* 空闲块数 */
	uint16	bg_free_inodes_count;		/* 空闲 inode 数 */
	uint16	bg_used_dirs_count;		/* 使用目录数 */
	uint16	bg_pad;				/* 填充 */
	uint32	bg_reserved[3];			/* 保留 */
} __attribute__((packed));

/* 磁盘 inode */
struct ext2_disk_inode {
	uint16	i_mode;				/* 模式 */
	uint16	i_uid;				/* 用户 ID */
	uint32	i_size;				/* 大小 */
	uint32	i_atime;			/* 访问时间 */
	uint32	i_ctime;			/* 创建时间 */
	uint32	i_mtime;			/* 修改时间 */
	uint32	i_dtime;			/* 删除时间 */
	uint16	i_gid;				/* 组 ID */
	uint16	i_links_count;			/* 链接数 */
	uint32	i_blocks;			/* 块数 */
	uint32	i_flags;			/* 标志 */
	uint32	i_osd1;				/* 保留 */
	uint32	i_block[EXT2_N_BLOCKS];		/* 块数组, 直接块、间接块、双间接块 */
	uint32	i_generation;			/* 代际数 */
	uint32	i_file_acl;			/* 文件 ACL */
	uint32	i_dir_acl;			/* 目录 ACL */
	uint32	i_faddr;			/* 文件地址 */
} __attribute__((packed));

/* 目录项 */
struct ext2_dir_entry {
	uint32	inode;				/* inode 号 */
	uint16	rec_len;			/* Record Length, 记录长度 */
	uint8	name_len;			/* 名称长度 */
	uint8	file_type;			/* 文件类型 */
	char	name[255];			/* 名称 */
} __attribute__((packed));

/*
 * 挂载实例私有数据（对齐 Linux ext2_sb_info 教学子集）。
 * 每挂载一份；inode 缓存在全局表中，查找键为 (sbi, inum)。
 */
struct ext2_sb_info {
	struct gendisk *disk;			/* 块设备 */
	uint32 block_size;			/* 块大小 */
	uint32 inodes_per_group;		/* 组中 inode 数 */
	uint32 blocks_per_group;		/* 组中块数 */
	uint32 inode_size;			/* inode 大小 */
	uint32 first_data_block;		/* 第一个数据块 */
	uint32 feature_incompat;		/* 不兼容特性 */
	uint32 inode_table;			/* 组 0 的 inode table 块号 */
};

/*
 * 内存 inode 缓存项。inode 必须为首字段，便于 inode* → mnode*。
 * 空闲：inode.type == 0；占用：sbi 指向所属挂载实例。
 */
struct ext2_mnode {
	struct inode inode;			/* VFS inode（首字段） */
	struct ext2_sb_info *sbi;		/* 所属挂载；与 inum 组成缓存键 */
	uint32 i_block[EXT2_N_BLOCKS];		/* 磁盘 i_block[] 缓存 */
};

static struct ext2_mnode eicache[EXT2_NICACHE];

/* inode 操作 */
static const struct inode_operations ext2_iops;

/* 从 inode 取本挂载实例的 sb_info */
static struct ext2_sb_info *EXT2_SB(struct inode *ip)
{
	return ip ? (struct ext2_sb_info *)ip->i_sb : 0;
}

static struct ext2_mnode *mnode_of(struct inode *ip)
{
	struct ext2_mnode *m;
	int i;

	if (!ip)
		return 0;
	m = (struct ext2_mnode *)ip;
	i = (int)(m - eicache);
	if (i < 0 || i >= EXT2_NICACHE || &eicache[i].inode != ip)
		return 0;
	return &eicache[i];
}

/*
 * 比较目录项名 a[0..n)（无 '\0'，长度由 name_len 给出）与 C 字符串 b。
 * 相等当且仅当前 n 字节相同且 b[n] == '\0'。
 * 不可读 a[n]：其后可能是下一项（如 "test" 紧挨 "test.elf"）。
 */
static int namecmp_n(const char *a, const char *b, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		if ((unsigned char)a[i] != (unsigned char)b[i])
			return (unsigned char)a[i] - (unsigned char)b[i];
	}
	return b[n] ? 1 : 0;
}

static void namecpy_dir(char *dst, const char *src)
{
	int i;

	for (i = 0; i < DIRSIZ - 1 && src[i]; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

/* 从块设备读取一个扇区到 buf */
static int ext2_read_sect(struct ext2_sb_info *sbi, sector_t sec, void *buf)
{
	if (!sbi || !sbi->disk)
		return -1;
	return blkdev_read_sect(sbi->disk, sec, buf);
}

/* 读一个文件系统块到 buf（须 ≥ block_size） */
static int ext2_read_block(struct ext2_sb_info *sbi, uint32 block, void *buf)
{
	sector_t sec;
	uint nsec, i;
	char *p = buf;

	if (!sbi || !sbi->block_size)
		return -1;
	sec = (sector_t)((uint64)block * sbi->block_size / BLOCK_SECTOR_SIZE);
	nsec = sbi->block_size / BLOCK_SECTOR_SIZE;
	for (i = 0; i < nsec; i++) {
		if (ext2_read_sect(sbi, sec + i, p + i * BLOCK_SECTOR_SIZE) < 0)
			return -1;
	}
	return 0;
}

/* 将磁盘 inode 模式转换为内存 inode 类型 */
static short mode_to_type(uint16 mode)
{
	switch (mode & EXT2_S_IFMT) {
	case EXT2_S_IFDIR:
		return T_DIR;
	case EXT2_S_IFREG:
		return T_FILE;
	default:
		return 0;
	}
}

static unsigned char ft_to_dtype(uint8 ft)
{
	switch (ft) {
	case EXT2_FT_REG_FILE:
		return DT_REG;
	case EXT2_FT_DIR:
		return DT_DIR;
	case EXT2_FT_CHRDEV:
		return DT_CHR;
	case EXT2_FT_BLKDEV:
		return DT_BLK;
	default:
		return DT_UNKNOWN;
	}
}

/* 获取 inode（键：sbi + inum；不同挂载即使 inum 相同也不共享） */
static struct inode *ext2_iget(struct ext2_sb_info *sbi, uint32 inum)
{
	int i, free;
	struct ext2_mnode *mn;
	struct inode *ip;
	struct ext2_disk_inode din;
	uint32 group, index, b, off;
	char *blk;
	uint8 *p;

	if (!sbi || inum == 0)
		return 0;

	free = -1;
	for (i = 0; i < EXT2_NICACHE; i++) {
		mn = &eicache[i];
		if (mn->inode.type != 0 && mn->sbi == sbi &&
		    mn->inode.inum == inum) {
			mn->inode.ref++;
			return &mn->inode;
		}
		if (mn->inode.type == 0 && free < 0)
			free = i;
	}
	if (free < 0)
		return 0;

	group = (inum - 1) / sbi->inodes_per_group; /* 计算 inode 所在的组 */
	index = (inum - 1) % sbi->inodes_per_group; /* 计算 inode 在组中的索引 */
	if (group != 0) {
		/* 第一版仅组 0（小镜像通常够用） */
		printk(KERN_WARNING "ext2: inode %u outside group 0\n", inum);
		return 0;
	}

	b = sbi->inode_table + (index * sbi->inode_size) / sbi->block_size;
	off = (index * sbi->inode_size) % sbi->block_size;

	blk = kmalloc(sbi->block_size);
	if (!blk)
		return 0;

	/* 从磁盘读取 inode 表中的 inode 到内存 */
	if (ext2_read_block(sbi, b, blk) < 0) {
		kfree(blk);
		return 0;
	}
	p = (uint8 *)blk + off;
	memset(&din, 0, sizeof(din));
	/* 磁盘 inode 可能大于经典 128；只取我们需要的头部 */
	{
		uint ncopy = sizeof(din);

		if (ncopy > sbi->inode_size)
			ncopy = sbi->inode_size;
		memcpy(&din, p, ncopy);
	}
	kfree(blk);

	mn = &eicache[free];
	ip = &mn->inode;
	memset(mn, 0, sizeof(*mn));
	mn->sbi = sbi;
	ip->inum = inum;
	ip->type = mode_to_type(din.i_mode);
	if (ip->type == 0) {
		/* 不支持的类型：槽位保持 type==0 空闲 */
		return 0;
	}
	ip->size = din.i_size;
	ip->ref = 1;
	ip->i_op = &ext2_iops;
	ip->i_sb = sbi;
	ip->parent = 0;
	for (i = 0; i < EXT2_N_BLOCKS; i++)
		mn->i_block[i] = din.i_block[i];
	return ip;
}

/* 逻辑块号 → 物理块号；0 表示空洞 */
static uint32 ext2_bmap(struct inode *ip, uint32 lbn)
{
	struct ext2_sb_info *sbi;
	struct ext2_mnode *mn;
	uint32 *iblk;
	uint32 ind, per;
	char *buf;
	uint32 phys;

	sbi = EXT2_SB(ip);
	mn = mnode_of(ip);
	if (!sbi || !mn || mn->sbi != sbi)
		return 0;
	iblk = mn->i_block;

	if (lbn < EXT2_NDIR_BLOCKS)
		return iblk[lbn];

	lbn -= EXT2_NDIR_BLOCKS;
	per = sbi->block_size / 4;	/* 计算每个块的间接块数 */

	/* 一级间接块 */
	if (lbn < per) {
		ind = iblk[EXT2_IND_BLOCK];
		if (!ind)
			return 0;
		buf = kmalloc(sbi->block_size);
		if (!buf)
			return 0;
		if (ext2_read_block(sbi, ind, buf) < 0) {
			kfree(buf);
			return 0;
		}
		phys = ((uint32 *)buf)[lbn];
		kfree(buf);
		return phys;
	}
	return 0;	/* 二级间接块及以上：第一版不支持 */
}

/* 读取数据 */
static int ext2_read_data(struct inode *ip, char *dst, uint off, uint n)
{
	struct ext2_sb_info *sbi;
	uint total, lbn, boff, chunk, phys;
	char *blk;

	sbi = EXT2_SB(ip);
	if (!sbi)
		return -1;
	if (off >= ip->size)
		return 0;
	if (off + n > ip->size)
		n = ip->size - off;

	blk = kmalloc(sbi->block_size);
	if (!blk)
		return -1;

	total = 0;
	while (total < n) {
		lbn = (off + total) / sbi->block_size;
		boff = (off + total) % sbi->block_size;
		chunk = sbi->block_size - boff;
		if (chunk > n - total)
			chunk = n - total;
		phys = ext2_bmap(ip, lbn);
		if (!phys) {
			memset(dst + total, 0, chunk);
		} else {
			if (ext2_read_block(sbi, phys, blk) < 0) {
				kfree(blk);
				return total ? (int)total : -1;
			}
			memcpy(dst + total, blk + boff, chunk);
		}
		total += chunk;
	}
	kfree(blk);
	return (int)total;
}

/*
 * 遍历目录：对每条有效 dirent 调 cb。
 * cb 返回非 0 则提前结束。
 */
static int ext2_dir_iterate(struct inode *dir,
			    int (*cb)(struct ext2_dir_entry *de, void *arg),
			    void *arg)
{
	struct ext2_sb_info *sbi;
	uint off;
	char *blk;
	uint32 lbn, phys, pos;
	struct ext2_dir_entry *de;
	int r;

	sbi = EXT2_SB(dir);
	if (!sbi || !dir || dir->type != T_DIR)
		return -1;

	blk = kmalloc(sbi->block_size);
	if (!blk)
		return -1;

	for (off = 0; off < dir->size; ) {
		lbn = off / sbi->block_size;	/* 计算逻辑块号 */
		pos = off % sbi->block_size;	/* 计算偏移 */
		if (pos == 0) {
			phys = ext2_bmap(dir, lbn);	/* 获取物理块号 */
			if (!phys) {
				kfree(blk);
				return -1;
			}
			if (ext2_read_block(sbi, phys, blk) < 0) {
				kfree(blk);
				return -1;
			}
		}
		de = (struct ext2_dir_entry *)(blk + pos);
		if (de->rec_len < 8 || pos + de->rec_len > sbi->block_size) {
			kfree(blk);
			return -1;
		}
		if (de->inode && de->name_len) {
			r = cb(de, arg);
			if (r) {
				kfree(blk);
				return r;
			}
		}
		off += de->rec_len;
	}
	kfree(blk);
	return 0;
}

/* 查找目录项参数 */
struct lookup_arg {
	const char *name;
	uint32 inum;
};

/* 查找目录项回调函数, 完成 inode 的赋值 */
static int lookup_cb(struct ext2_dir_entry *de, void *arg)
{
	struct lookup_arg *a = arg;
	int nlen = de->name_len;

	if (namecmp_n(de->name, a->name, nlen) == 0) {
		a->inum = de->inode;
		return 1;
	}
	return 0;
}

static struct inode *ext2_lookup(struct inode *dir, const char *name)
{
	struct ext2_sb_info *sbi;
	struct lookup_arg a;
	struct inode *ip;

	sbi = EXT2_SB(dir);
	if (!sbi || !dir || !name || dir->type != T_DIR)
		return 0;
	a.name = name;
	a.inum = 0;
	if (ext2_dir_iterate(dir, lookup_cb, &a) != 1 || !a.inum)
		return 0;
	ip = ext2_iget(sbi, a.inum);
	if (ip) {
		if (!ip->parent)
			ip->parent = dir;
		/* 缓存路径分量名，供 getcwd（类 dentry.d_name） */
		namecpy_dir(ip->name, name);
	}
	return ip;
}

struct readdir_arg {
	uint skip;	/* 跳过前 skip 条有效项 */
	uint seen;	/* 已读取的项数 */
	char *dst;	/* 缓冲区 */
	uint nleft;	/* 剩余缓冲区大小 */
	uint total;	/* 已读取的项数 */
	uint base_off;	/* 用户目录流逻辑偏移（定长 dirent）, 用于计算偏移 */
	uint32 feature_incompat; /* 本挂载 incompat 特性 */
};

/* 读取目录项回调函数 */
static int readdir_cb(struct ext2_dir_entry *de, void *arg)
{
	struct readdir_arg *a = arg;
	struct dirent ude;
	uint esz = sizeof(struct dirent);
	int i, nlen;
	uint8 dtype;

	/* 跳过前 skip 条有效项 */
	if (a->seen < a->skip) {
		a->seen++;
		return 0;
	}
	/* 缓冲满，停止 */
	if (a->nleft < esz)
		return 1;	/* 缓冲满，停止 */

	memset(&ude, 0, sizeof(ude));
	ude.d_ino = de->inode;
	ude.d_reclen = (unsigned short)esz;
	ude.d_off = a->base_off + a->total + esz;
	if (a->feature_incompat & EXT2_FEATURE_INCOMPAT_FILETYPE)
		dtype = ft_to_dtype(de->file_type);
	else
		dtype = DT_UNKNOWN;
	ude.d_type = dtype;
	nlen = de->name_len;
	if (nlen > NAME_MAX)
		nlen = NAME_MAX;
	for (i = 0; i < nlen; i++)
		ude.d_name[i] = de->name[i];
	ude.d_name[nlen] = '\0';
	memcpy(a->dst, &ude, esz);
	a->dst += esz;
	a->nleft -= esz;
	a->total += esz;
	a->seen++;
	return 0;
}

/* 读取目录 */
static int ext2_readdir(struct inode *ip, char *dst, uint off, uint n)
{
	struct ext2_sb_info *sbi;
	struct readdir_arg a;
	uint esz = sizeof(struct dirent);

	sbi = EXT2_SB(ip);
	if (!sbi || off % esz)
		return -1;
	a.skip = off / esz;
	a.seen = 0;
	a.dst = dst;
	a.nleft = n;
	a.total = 0;
	a.base_off = off;
	a.feature_incompat = sbi->feature_incompat;
	ext2_dir_iterate(ip, readdir_cb, &a);
	return (int)a.total;
}

static int ext2_read(struct inode *ip, char *dst, uint off, uint n)
{
	if (!ip)
		return -1;
	if (ip->type == T_DIR)
		return ext2_readdir(ip, dst, off, n);
	if (ip->type == T_FILE)
		return ext2_read_data(ip, dst, off, n);
	return -1;
}

static int ext2_write(struct inode *ip, char *src, uint off, uint n)
{
	(void)ip;
	(void)src;
	(void)off;
	(void)n;
	return -1;
}

static int ext2_create(struct inode *dir, const char *name, short type,
		       struct inode **out)
{
	(void)dir;
	(void)name;
	(void)type;
	(void)out;
	return -1;
}

static int ext2_mkdir(struct inode *dir, const char *name)
{
	(void)dir;
	(void)name;
	return -1;
}

static int ext2_rmdir(struct inode *dir, const char *name)
{
	(void)dir;
	(void)name;
	return -1;
}

static int ext2_mknod(struct inode *dir, const char *name, short type,
		      dev_t rdev)
{
	(void)dir;
	(void)name;
	(void)type;
	(void)rdev;
	return -1;
}

static void ext2_evict(struct inode *ip)
{
	struct ext2_mnode *mn;

	mn = mnode_of(ip);
	if (mn) {
		mn->sbi = 0;
		memset(mn->i_block, 0, sizeof(mn->i_block));
	}
	ip->type = 0;
	ip->inum = 0;
	ip->size = 0;
	ip->ref = 0;
	ip->parent = 0;
	ip->name[0] = '\0';
	ip->i_op = 0;
	ip->i_sb = 0;
	ip->data = 0;
	ip->dents = 0;
	ip->rdev = 0;
}

/* ext2 文件系统的 inode 操作表 */
static const struct inode_operations ext2_iops = {
	.lookup		= ext2_lookup,
	.create		= ext2_create,
	.mkdir		= ext2_mkdir,
	.rmdir		= ext2_rmdir,
	.mknod		= ext2_mknod,
	.evict		= ext2_evict,
	.read		= ext2_read,
	.write		= ext2_write,
};

/* 读取超级块原始数据 */
static int ext2_read_sb_raw(struct gendisk *gd, struct ext2_super_block *sb)
{
	char sect[BLOCK_SECTOR_SIZE];
	sector_t sec;

	if (!gd || !sb)
		return -1;
	sec = 1024 / BLOCK_SECTOR_SIZE;
	if (blkdev_read_sect(gd, sec, sect) < 0)
		return -1;
	memset(sb, 0, sizeof(*sb));
	memcpy(sb, sect, sizeof(*sb) < BLOCK_SECTOR_SIZE ?
	       sizeof(*sb) : BLOCK_SECTOR_SIZE);
	return 0;
}

/* 读取超级块到本挂载实例 */
static int ext2_read_super(struct ext2_sb_info *sbi)
{
	struct ext2_super_block sb;
	struct ext2_group_desc gd0;
	uint32 gdt_block;
	char *blk;

	if (ext2_read_sb_raw(sbi->disk, &sb) < 0)
		return -1;
	if (sb.s_magic != EXT2_SUPER_MAGIC)
		return -1;

	/* 计算块大小 */
	sbi->block_size = 1024u << sb.s_log_block_size;
	if (sbi->block_size < 1024 || sbi->block_size > 4096)
		return -1;

	/* 计算组中 inode 数 */
	sbi->inodes_per_group = sb.s_inodes_per_group;
	sbi->blocks_per_group = sb.s_blocks_per_group;
	sbi->first_data_block = sb.s_first_data_block;
	sbi->feature_incompat = sb.s_feature_incompat;
	sbi->inode_size = sb.s_rev_level >= 1 ? sb.s_inode_size : 128;
	if (sbi->inode_size < 128)
		sbi->inode_size = 128;

	gdt_block = sbi->first_data_block + 1;
	blk = kmalloc(sbi->block_size);
	if (!blk)
		return -1;

	/* 读取块组描述符 */
	if (ext2_read_block(sbi, gdt_block, blk) < 0) {
		kfree(blk);
		return -1;
	}
	memcpy(&gd0, blk, sizeof(gd0));
	kfree(blk);
	sbi->inode_table = gd0.bg_inode_table;
	return 0;
}

/*
 * 填充 ext2 文件系统的超级块（一份新的挂载实例）。
 * 按需加载：mount 层已用魔数识别为本类型后调用。
 * 分配 sb_info，读完整超级块 / 组描述符并 iget 根；
 * 成功返回已持引用的根 inode（ip->i_sb 指向本实例）。
 */
struct inode *ext2_fill_super(struct gendisk *gd)
{
	struct ext2_sb_info *sbi;
	struct inode *root;

	if (!gd)
		return 0;

	sbi = kmalloc(sizeof(*sbi));
	if (!sbi)
		return 0;
	memset(sbi, 0, sizeof(*sbi));
	sbi->disk = gd;

	if (ext2_read_super(sbi) < 0) {
		kfree(sbi);
		return 0;
	}

	root = ext2_iget(sbi, EXT2_ROOT_INO);
	if (!root) {
		kfree(sbi);
		return 0;
	}
	printk(KERN_INFO "ext2: fill_super %s sbi=%p root=%p\n",
	       gd->disk_name, sbi, root);
	return root;
}

/* 释放该挂载实例的超级块（该 sbi 上 inode 应已全部 evict） */
void ext2_put_super(void *sb)
{
	struct ext2_sb_info *sbi = sb;
	int i;

	if (!sbi)
		return;
	/* 防御：清掉仍挂着本 sbi 的缓存项，避免悬空 */
	for (i = 0; i < EXT2_NICACHE; i++) {
		if (eicache[i].sbi == sbi) {
			eicache[i].sbi = 0;
			eicache[i].inode.type = 0;
			eicache[i].inode.i_sb = 0;
			eicache[i].inode.i_op = 0;
			eicache[i].inode.ref = 0;
		}
	}
	kfree(sbi);
}

static struct file_system_type ext2_fs_type = {
	.name		= "ext2",
	.magic_off	= 1024 + 56,	/* s_magic 在超级块内偏移 */
	.magic		= EXT2_SUPER_MAGIC,
	.fill_super	= ext2_fill_super,
	.kill_sb	= ext2_put_super,
};

/* 注册 ext2 文件系统 */
void ext2_init(void)
{
	if (register_filesystem(&ext2_fs_type) < 0)
		printk(KERN_WARNING "ext2: register_filesystem failed\n");
}
