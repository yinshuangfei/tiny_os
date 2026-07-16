/*
 * 宿主工具：生成简易单块组 ext2 镜像（供 Tiny-OS 只读挂载）。
 * 用法: mkfs_ext2 <image> [size_mb]
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define EXT2_SUPER_MAGIC	0xEF53
#define BLOCK_SIZE		1024
#define INODE_SIZE		128
#define INODES_COUNT		32

#define EXT2_FT_REG_FILE	1
#define EXT2_FT_DIR		2

#define INO_ROOT		2
#define INO_HELLO		3
#define INO_DIR			4
#define INO_A			5

struct ext2_super_block {
	uint32_t s_inodes_count;
	uint32_t s_blocks_count;
	uint32_t s_r_blocks_count;
	uint32_t s_free_blocks_count;
	uint32_t s_free_inodes_count;
	uint32_t s_first_data_block;
	uint32_t s_log_block_size;
	uint32_t s_log_frag_size;
	uint32_t s_blocks_per_group;
	uint32_t s_frags_per_group;
	uint32_t s_inodes_per_group;
	uint32_t s_mtime;
	uint32_t s_wtime;
	uint16_t s_mnt_count;
	uint16_t s_max_mnt_count;
	uint16_t s_magic;
	uint16_t s_state;
	uint16_t s_errors;
	uint16_t s_minor_rev_level;
	uint32_t s_lastcheck;
	uint32_t s_checkinterval;
	uint32_t s_creator_os;
	uint32_t s_rev_level;
	uint16_t s_def_resuid;
	uint16_t s_def_resgid;
	uint32_t s_first_ino;
	uint16_t s_inode_size;
	uint16_t s_block_group_nr;
	uint32_t s_feature_compat;
	uint32_t s_feature_incompat;
	uint32_t s_feature_ro_compat;
	uint8_t  s_uuid[16];
	char     s_volume_name[16];
} __attribute__((packed));

struct ext2_group_desc {
	uint32_t bg_block_bitmap;
	uint32_t bg_inode_bitmap;
	uint32_t bg_inode_table;
	uint16_t bg_free_blocks_count;
	uint16_t bg_free_inodes_count;
	uint16_t bg_used_dirs_count;
	uint16_t bg_pad;
	uint32_t bg_reserved[3];
} __attribute__((packed));

struct ext2_inode {
	uint16_t i_mode;
	uint16_t i_uid;
	uint32_t i_size;
	uint32_t i_atime;
	uint32_t i_ctime;
	uint32_t i_mtime;
	uint32_t i_dtime;
	uint16_t i_gid;
	uint16_t i_links_count;
	uint32_t i_blocks;	/* 以 512 字节为单位 */
	uint32_t i_flags;
	uint32_t i_osd1;
	uint32_t i_block[15];
	uint32_t i_generation;
	uint32_t i_file_acl;
	uint32_t i_dir_acl;
	uint32_t i_faddr;
	uint8_t  i_osd2[12];
} __attribute__((packed));

static int fd = -1;
static uint32_t nblocks;
static uint32_t inode_table_blks;
static const uint32_t blk_bitmap_blk = 3;
static const uint32_t ino_bitmap_blk = 4;
static const uint32_t inode_table_blk = 5;
static uint32_t first_data_blk;

static uint8_t *blk_bitmap;
static uint8_t *ino_bitmap;

static void die(const char *msg)
{
	perror(msg);
	exit(1);
}

static void write_block(uint32_t blk, const void *buf)
{
	uint8_t pad[BLOCK_SIZE];

	memset(pad, 0, sizeof(pad));
	if (buf)
		memcpy(pad, buf, BLOCK_SIZE);
	if (lseek(fd, (off_t)blk * BLOCK_SIZE, SEEK_SET) < 0)
		die("lseek");
	if (write(fd, pad, BLOCK_SIZE) != BLOCK_SIZE)
		die("write");
}

static void read_block(uint32_t blk, void *buf)
{
	if (lseek(fd, (off_t)blk * BLOCK_SIZE, SEEK_SET) < 0)
		die("lseek");
	if (read(fd, buf, BLOCK_SIZE) != BLOCK_SIZE)
		die("read");
}

static void set_bit(uint8_t *map, uint32_t bit)
{
	map[bit / 8] |= (uint8_t)(1u << (bit % 8));
}

static int bit_isset(const uint8_t *map, uint32_t bit)
{
	return (map[bit / 8] >> (bit % 8)) & 1;
}

static uint32_t alloc_block(void)
{
	uint32_t b;

	for (b = first_data_blk; b < nblocks; b++) {
		if (!bit_isset(blk_bitmap, b)) {
			set_bit(blk_bitmap, b);
			return b;
		}
	}
	fprintf(stderr, "mkfs_ext2: out of blocks\n");
	exit(1);
}

static void put_inode(uint32_t inum, const struct ext2_inode *ino)
{
	uint32_t index = inum - 1;
	uint32_t b = inode_table_blk + (index * INODE_SIZE) / BLOCK_SIZE;
	uint32_t off = (index * INODE_SIZE) % BLOCK_SIZE;
	uint8_t buf[BLOCK_SIZE];

	read_block(b, buf);
	memcpy(buf + off, ino, sizeof(*ino));
	write_block(b, buf);
	set_bit(ino_bitmap, index);
}

static uint16_t dirent_rec_len(uint8_t namelen)
{
	return (uint16_t)((8 + namelen + 3) & ~3);
}

/*
 * 向目录块追加一项。entries 以 {inum, ft, name} 描述；最后一项 rec_len 延至块尾。
 */
static void build_dir_block(uint8_t *blk, uint32_t *inums, uint8_t *fts,
			    const char **names, int nent)
{
	uint16_t off = 0;
	int i;

	memset(blk, 0, BLOCK_SIZE);
	for (i = 0; i < nent; i++) {
		uint8_t nlen = (uint8_t)strlen(names[i]);
		uint16_t real = dirent_rec_len(nlen);
		uint16_t span = (i == nent - 1) ? (uint16_t)(BLOCK_SIZE - off) : real;

		if (off + span > BLOCK_SIZE) {
			fprintf(stderr, "mkfs_ext2: dir block overflow\n");
			exit(1);
		}
		memcpy(blk + off, &inums[i], 4);
		memcpy(blk + off + 4, &span, 2);
		blk[off + 6] = nlen;
		blk[off + 7] = fts[i];
		memcpy(blk + off + 8, names[i], nlen);
		off = (uint16_t)(off + span);
	}
}

static void write_reg(uint32_t inum, uint32_t dblk, const char *text)
{
	struct ext2_inode ino;
	uint8_t buf[BLOCK_SIZE];
	uint32_t len = (uint32_t)strlen(text);

	memset(buf, 0, sizeof(buf));
	memcpy(buf, text, len);
	write_block(dblk, buf);

	memset(&ino, 0, sizeof(ino));
	ino.i_mode = S_IFREG | 0644;
	ino.i_size = len;
	ino.i_links_count = 1;
	ino.i_blocks = BLOCK_SIZE / 512;
	ino.i_block[0] = dblk;
	put_inode(inum, &ino);
}

static void write_dir(uint32_t inum, uint32_t dblk, uint16_t nlink,
		      uint32_t *inums, uint8_t *fts, const char **names, int nent)
{
	struct ext2_inode ino;
	uint8_t buf[BLOCK_SIZE];

	build_dir_block(buf, inums, fts, names, nent);
	write_block(dblk, buf);

	memset(&ino, 0, sizeof(ino));
	ino.i_mode = S_IFDIR | 0755;
	ino.i_size = BLOCK_SIZE;
	ino.i_links_count = nlink;
	ino.i_blocks = BLOCK_SIZE / 512;
	ino.i_block[0] = dblk;
	put_inode(inum, &ino);
}

static int already_ext2(const char *path)
{
	int f;
	uint16_t magic = 0;

	f = open(path, O_RDONLY);
	if (f < 0)
		return 0;
	if (lseek(f, 1024 + 56, SEEK_SET) == 1024 + 56)
		(void)read(f, &magic, sizeof(magic));
	close(f);
	return magic == EXT2_SUPER_MAGIC;
}

int main(int argc, char **argv)
{
	const char *path;
	int size_mb = 8;
	uint32_t b, used_meta;
	struct ext2_super_block sb;
	struct ext2_group_desc gd;
	uint8_t zero[BLOCK_SIZE];
	uint32_t root_blk, hello_blk, dir_blk, a_blk;
	uint32_t free_blocks, free_inodes;
	const char *hello = "Hello from ext2 on /mnt!\n";
	const char *atext = "file in dir\n";

	/* 根目录：. .. hello.txt dir */
	uint32_t root_inos[] = { INO_ROOT, INO_ROOT, INO_HELLO, INO_DIR };
	uint8_t root_fts[] = { EXT2_FT_DIR, EXT2_FT_DIR, EXT2_FT_REG_FILE, EXT2_FT_DIR };
	const char *root_names[] = { ".", "..", "hello.txt", "dir" };

	/* dir：. .. a.txt */
	uint32_t dir_inos[] = { INO_DIR, INO_ROOT, INO_A };
	uint8_t dir_fts[] = { EXT2_FT_DIR, EXT2_FT_DIR, EXT2_FT_REG_FILE };
	const char *dir_names[] = { ".", "..", "a.txt" };

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: %s <image> [size_mb]\n", argv[0]);
		return 1;
	}
	path = argv[1];
	if (argc == 3)
		size_mb = atoi(argv[2]);
	if (size_mb < 1) {
		fprintf(stderr, "mkfs_ext2: size_mb must be >= 1\n");
		return 1;
	}

	if (already_ext2(path)) {
		printf("%s: keep existing ext2 image\n", path);
		return 0;
	}

	nblocks = (uint32_t)size_mb * (1024 * 1024 / BLOCK_SIZE);
	inode_table_blks = (INODES_COUNT * INODE_SIZE + BLOCK_SIZE - 1) / BLOCK_SIZE;
	first_data_blk = inode_table_blk + inode_table_blks;
	used_meta = first_data_blk;

	if (first_data_blk + 8 >= nblocks) {
		fprintf(stderr, "mkfs_ext2: image too small\n");
		return 1;
	}

	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		die("open image");
	if (ftruncate(fd, (off_t)nblocks * BLOCK_SIZE) < 0)
		die("ftruncate");

	blk_bitmap = calloc(1, BLOCK_SIZE);
	ino_bitmap = calloc(1, BLOCK_SIZE);
	if (!blk_bitmap || !ino_bitmap)
		die("calloc");

	memset(zero, 0, sizeof(zero));
	for (b = 0; b < used_meta; b++) {
		write_block(b, zero);
		set_bit(blk_bitmap, b);
	}

	root_blk = alloc_block();
	hello_blk = alloc_block();
	dir_blk = alloc_block();
	a_blk = alloc_block();

	for (b = 0; b < inode_table_blks; b++)
		write_block(inode_table_blk + b, zero);

	/* inode 1 保留 */
	set_bit(ino_bitmap, 0);

	/* 根 links=3（. 与子目录 dir 的 ..） */
	write_dir(INO_ROOT, root_blk, 3, root_inos, root_fts, root_names, 4);
	write_reg(INO_HELLO, hello_blk, hello);
	write_dir(INO_DIR, dir_blk, 2, dir_inos, dir_fts, dir_names, 3);
	write_reg(INO_A, a_blk, atext);

	free_inodes = 0;
	for (b = 0; b < INODES_COUNT; b++)
		if (!bit_isset(ino_bitmap, b))
			free_inodes++;
	free_blocks = 0;
	for (b = 0; b < nblocks; b++)
		if (!bit_isset(blk_bitmap, b))
			free_blocks++;

	memset(&sb, 0, sizeof(sb));
	sb.s_inodes_count = INODES_COUNT;
	sb.s_blocks_count = nblocks;
	sb.s_free_blocks_count = free_blocks;
	sb.s_free_inodes_count = free_inodes;
	sb.s_first_data_block = 1;
	sb.s_log_block_size = 0;
	sb.s_blocks_per_group = nblocks;
	sb.s_frags_per_group = nblocks;
	sb.s_inodes_per_group = INODES_COUNT;
	sb.s_magic = EXT2_SUPER_MAGIC;
	sb.s_state = 1;
	sb.s_errors = 1;
	sb.s_rev_level = 1;
	sb.s_first_ino = 11;
	sb.s_inode_size = INODE_SIZE;
	sb.s_feature_incompat = 0x2;	/* FILETYPE */
	memcpy(sb.s_volume_name, "tiny-ext2", 9);

	memset(&gd, 0, sizeof(gd));
	gd.bg_block_bitmap = blk_bitmap_blk;
	gd.bg_inode_bitmap = ino_bitmap_blk;
	gd.bg_inode_table = inode_table_blk;
	gd.bg_free_blocks_count = (uint16_t)free_blocks;
	gd.bg_free_inodes_count = (uint16_t)free_inodes;
	gd.bg_used_dirs_count = 2;

	{
		uint8_t buf[BLOCK_SIZE];
		memset(buf, 0, sizeof(buf));
		memcpy(buf, &sb, sizeof(sb));
		write_block(1, buf);
		memset(buf, 0, sizeof(buf));
		memcpy(buf, &gd, sizeof(gd));
		write_block(2, buf);
	}
	write_block(blk_bitmap_blk, blk_bitmap);
	write_block(ino_bitmap_blk, ino_bitmap);

	close(fd);
	free(blk_bitmap);
	free(ino_bitmap);
	printf("created %s (%d MiB, ext2 + hello.txt, dir/a.txt)\n",
	       path, size_mb);
	return 0;
}
