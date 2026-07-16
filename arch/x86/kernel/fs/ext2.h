/*
 * 只读 ext2 教学子集。
 * 向 mount 层注册类型；识别魔数后由 fill_super 按需加载。
 */
#ifndef __EXT2_H__
#define __EXT2_H__

struct gendisk;
struct inode;

#define EXT2_SUPER_MAGIC	0xEF53
#define EXT2_ROOT_INO		2

/* 向 VFS 注册本 FS（不装入任何盘；加载见 fill_super） */
void ext2_init(void);

struct inode *ext2_fill_super(struct gendisk *gd);

#endif
