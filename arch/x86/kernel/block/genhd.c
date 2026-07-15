/*
 * gendisk：整盘抽象（对齐 Linux block/genhd.c 核心路径）。
 * alloc_disk / add_disk / del_gendisk / set_capacity / 查找。
 */
#include "blk.h"
#include "../defs.h"
#include "../printk.h"
#include "../fs/fs.h"

/* 全局通用磁盘链表 */
static struct list_head gendisk_list;
/* 全局通用磁盘锁 */
static struct spinlock gendisk_lock;
/* 全局通用磁盘计数 */
static int gendisk_count;

/* 初始化通用磁盘层 */
void blk_init(void)
{
	INIT_LIST_HEAD(&gendisk_list);
	initlock(&gendisk_lock, "gendisk");
	gendisk_count = 0;
	printk(KERN_INFO "block: layer ready (gendisk/bio/submit_bio)\n");
}

/* 分配一个请求队列 */
struct request_queue *blk_alloc_queue(void)
{
	struct request_queue *q;

	q = kmalloc(sizeof(*q));
	if (!q)
		return 0;
	q->make_request_fn = 0;
	q->queuedata = 0;
	q->logical_block_size = BLOCK_SECTOR_SIZE;
	initlock(&q->queue_lock, "blkq");
	return q;
}

/* 释放请求队列 */
void blk_cleanup_queue(struct request_queue *q)
{
	if (!q)
		return;
	kfree(q);
}

/* 设置请求处理函数 */
void blk_queue_make_request(struct request_queue *q, make_request_fn *fn)
{
	if (q)
		q->make_request_fn = fn;
}

/* 设置逻辑块大小 */
void blk_queue_logical_block_size(struct request_queue *q, unsigned int size)
{
	if (q && size)
		q->logical_block_size = size;
}

/* 分配一个 gendisk 结构体, minors 为次设备号总数 */
struct gendisk *alloc_disk(int minors)
{
	struct gendisk *disk;

	if (minors <= 0)
		minors = 1;
	disk = kmalloc(sizeof(*disk));
	if (!disk) {
		printk(KERN_ERR "block: alloc_disk: kmalloc failed\n");
		return 0;
	}
	memset(disk, 0, sizeof(*disk));
	disk->minors = minors;
	INIT_LIST_HEAD(&disk->list);
	disk->queue = blk_alloc_queue();
	if (!disk->queue) {
		printk("block: alloc_disk: blk_alloc_queue failed\n");
		kfree(disk);
		return 0;
	}
	return disk;
}

/* 释放通用磁盘 */
void put_disk(struct gendisk *disk)
{
	if (!disk)
		return;
	if (disk->queue) {
		blk_cleanup_queue(disk->queue);
		disk->queue = 0;
	}
	kfree(disk);
}

/* 设置通用磁盘容量 */
void set_capacity(struct gendisk *disk, sector_t sectors)
{
	if (disk)
		disk->capacity = sectors;
}

/* 获取通用磁盘容量 */
sector_t get_capacity(struct gendisk *disk)
{
	return disk ? disk->capacity : 0;
}

/* 在 /dev 下为 gendisk 创建块设备节点（如 /dev/hda） */
static int register_disk_devnode(struct gendisk *disk)
{
	char path[40];
	struct inode *ip;

	snprintf(path, sizeof(path), "/dev/%s", disk->disk_name);
	ip = fs_mknod(path, T_BLK, (unsigned int)disk->major,
		      (unsigned int)disk->first_minor);
	if (!ip) {
		printk(KERN_ERR "block: mknod %s failed\n", path);
		return -1;
	}
	ip->size = (uint)disk->capacity * BLOCK_SECTOR_SIZE;
	fs_iput(ip);
	printk(KERN_INFO "block: created %s\n", path);
	return 0;
}

/* 添加通用磁盘 */
void add_disk(struct gendisk *disk)
{
	if (!disk || !disk->queue || !disk->queue->make_request_fn) {
		printk(KERN_ERR "block: add_disk: incomplete gendisk\n");
		return;
	}
	if (!disk->disk_name[0])
		snprintf(disk->disk_name, sizeof(disk->disk_name),
			 "hd%c", 'a' + (disk->first_minor & 0x1f));

	acquire(&gendisk_lock);
	list_add_tail(&disk->list, &gendisk_list);
	gendisk_count++;
	release(&gendisk_lock);

	printk(KERN_INFO "block: %s (%d:%d) capacity %u sectors (%u KiB)\n",
	       disk->disk_name, disk->major, disk->first_minor,
	       (unsigned int)disk->capacity,
	       (unsigned int)(disk->capacity / 2));

	register_disk_devnode(disk);
}

/* 删除通用磁盘 */
void del_gendisk(struct gendisk *disk)
{
	if (!disk)
		return;
	acquire(&gendisk_lock);
	if (!list_empty(&disk->list)) {
		list_del(&disk->list);
		if (gendisk_count > 0)
			gendisk_count--;
	}
	release(&gendisk_lock);
}

/* 根据设备号查找通用磁盘 */
struct gendisk *blk_lookup_dev(dev_t dev)
{
	struct list_head *pos;
	struct gendisk *disk;
	unsigned int ma = MAJOR(dev);
	unsigned int mi = MINOR(dev);

	acquire(&gendisk_lock);
	list_for_each(pos, &gendisk_list) {
		disk = list_entry(pos, struct gendisk, list);
		if ((unsigned int)disk->major == ma &&
		    (unsigned int)disk->first_minor == mi) {
			release(&gendisk_lock);
			return disk;
		}
	}
	release(&gendisk_lock);
	return 0;
}

/* 根据名称查找通用磁盘 */
struct gendisk *blk_lookup_name(const char *name)
{
	struct list_head *pos;
	struct gendisk *disk;
	int i;

	if (!name)
		return 0;
	acquire(&gendisk_lock);
	list_for_each(pos, &gendisk_list) {
		disk = list_entry(pos, struct gendisk, list);
		for (i = 0; disk->disk_name[i] && name[i] &&
		     disk->disk_name[i] == name[i]; i++)
			;
		if (disk->disk_name[i] == name[i]) {
			release(&gendisk_lock);
			return disk;
		}
	}
	release(&gendisk_lock);
	return 0;
}

/* 获取通用磁盘数量 */
int blk_ndisks(void)
{
	return gendisk_count;
}
