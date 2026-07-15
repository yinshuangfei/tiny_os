/*
 * 内核块设备层（教学版，命名/分层对齐 Linux block layer）。
 *
 * Linux 对应关系（简化）：
 *   gendisk          — 整盘（如 hda）
 *   request_queue    — 每盘一条队列，挂 make_request_fn
 *   bio              — 一次 I/O 描述（扇区 + 缓冲）
 *   submit_bio()     — 提交 I/O；本实现为同步 PIO，在 make_request 内完成
 *
 * 驱动侧：alloc_disk → blk_alloc_queue → blk_queue_make_request →
 *         set_capacity → add_disk。
 * 使用侧：bio_alloc → 填 bi_* → submit_bio_wait / submit_bio。
 */
#ifndef __BLK_H__
#define __BLK_H__

#include "../types.h"
#include "../list.h"
#include "../spinlock.h"
#include "../major.h"

/* 逻辑块大小（与 ATA 扇区一致） */
#define BLOCK_SECTOR_SIZE	512
#define SECTOR_SHIFT		9

typedef uint32 sector_t;

/* bio / 请求操作码（对齐 Linux REQ_OP_* 子集） */
#define REQ_OP_READ		0		/* 读 */
#define REQ_OP_WRITE		1		/* 写 */
#define REQ_OP_MASK		0xff		/* 掩码 */

/* blk_status_t（对齐 Linux：0 成功） */
typedef int blk_status_t;
#define BLK_STS_OK		0		/* 成功 */
#define BLK_STS_IOERR		1		/* 输入/输出错误 */
#define BLK_STS_NOTSUPP		2		/* 不支持 */

struct gendisk;
struct request_queue;
struct bio;

/* ---------- block_device_operations（Linux blkdev_ops 子集） ---------- */

struct block_device_operations {
	int (*open)(struct gendisk *disk);		/* 打开 */
	void (*release)(struct gendisk *disk);		/* 释放 */
	int (*getgeo)(struct gendisk *disk, void *geo);	/* 获取几何信息 */
};

/* ---------- request_queue ---------- */

typedef void (make_request_fn)(struct request_queue *q, struct bio *bio);

/* 请求队列结构体 */
struct request_queue {
	make_request_fn *make_request_fn;		/* 请求处理函数 */
	void *queuedata;				/* 驱动私有（常为 ide_disk*） */
	unsigned int logical_block_size;		/* 逻辑块大小 */
	struct spinlock queue_lock;			/* 队列锁 */
};

/* ---------- gendisk ---------- */

struct gendisk {
	int major;					/* 主设备号 */
	int first_minor;				/* 第一个次设备号 */
	int minors;					/* 次设备号数 */
	char disk_name[32];				/* 磁盘名称 */
	sector_t capacity;				/* 总扇区数 (LBA28) */
	struct request_queue *queue;			/* 请求队列 */
	const struct block_device_operations *fops;	/* 块设备操作 */
	void *private_data;				/* 私有数据 */
	struct list_head list;				/* 全局通用磁盘链表 */
};

/* ---------- bio ---------- */

/* 完成回调函数类型 */
typedef void (bio_end_io_t)(struct bio *);

/* bio 结构体 */
struct bio {
	struct gendisk *bi_disk;		/* 通用磁盘 */
	sector_t bi_sector;			/* 起始 LBA (扇区号) */
	unsigned int bi_size;			/* 字节数，须为扇区整数倍 */
	unsigned int bi_opf;			/* REQ_OP_* */
	void *bi_data;				/* 内核缓冲（连续） */
	bio_end_io_t *bi_end_io;		/* 完成回调 */
	void *bi_private;			/* 私有数据 */
	blk_status_t bi_status;			/* 状态 */
};

/* ---------- 初始化 / 队列 / 磁盘 ---------- */

void blk_init(void);

struct request_queue *blk_alloc_queue(void);
void blk_cleanup_queue(struct request_queue *q);
void blk_queue_make_request(struct request_queue *q, make_request_fn *fn);
void blk_queue_logical_block_size(struct request_queue *q, unsigned int size);

struct gendisk *alloc_disk(int minors);
void put_disk(struct gendisk *disk);
void set_capacity(struct gendisk *disk, sector_t sectors);
sector_t get_capacity(struct gendisk *disk);
void add_disk(struct gendisk *disk);
void del_gendisk(struct gendisk *disk);

struct gendisk *blk_lookup_dev(dev_t dev);
struct gendisk *blk_lookup_name(const char *name);
int blk_ndisks(void);

/* ---------- bio / 提交 ---------- */

struct bio *bio_alloc(void);
void bio_put(struct bio *bio);
void bio_set_dev(struct bio *bio, struct gendisk *disk);
void bio_set_op_attrs(struct bio *bio, unsigned int op);
void submit_bio(struct bio *bio);
int submit_bio_wait(struct bio *bio);

/* 单扇区便捷接口（内部走 bio） */
int blkdev_read_sect(struct gendisk *disk, sector_t sector, void *buf);
int blkdev_write_sect(struct gendisk *disk, sector_t sector, const void *buf);

#endif
