/*
 * 块层核心：submit_bio（对齐 Linux blk-core submit_bio 简化路径）。
 *
 * Linux 异步队列 + 调度合并；此处 make_request_fn 同步完成（IDE PIO），
 * submit_bio_wait 在返回时即可读到 bi_status。
 */
#include "blk.h"
#include "../defs.h"
#include "../printk.h"

/* 提交一个 bio 结构体 */
void submit_bio(struct bio *bio)
{
	struct gendisk *disk;
	struct request_queue *q;

	if (!bio) {
		printk(KERN_ERR "block: submit_bio: bio is null\n");
		return;
	}

	disk = bio->bi_disk;

	/* 检查通用磁盘 */
	if (!disk || !disk->queue || !disk->queue->make_request_fn) {
		bio->bi_status = BLK_STS_NOTSUPP;
		if (bio->bi_end_io)
			bio->bi_end_io(bio);
		return;
	}

	/* 检查数据缓冲区指针和大小 */
	if (!bio->bi_data || bio->bi_size == 0 ||
	    (bio->bi_size & (BLOCK_SECTOR_SIZE - 1)) != 0) {
		bio->bi_status = BLK_STS_IOERR;
		if (bio->bi_end_io)
			bio->bi_end_io(bio);
		return;
	}

	/* 检查扇区号和大小 */
	if (bio->bi_sector + (bio->bi_size >> SECTOR_SHIFT) > disk->capacity) {
		bio->bi_status = BLK_STS_IOERR;
		if (bio->bi_end_io)
			bio->bi_end_io(bio);
		return;
	}

	q = disk->queue;
	/*
	 * Linux：generic_make_request → 队列 → 驱动。
	 * 教学实现：直接调驱动挂的 make_request_fn。
	 */
	q->make_request_fn(q, bio);
}

/* 等待完成回调 */
static void bio_wait_end_io(struct bio *bio)
{
	int *done = bio->bi_private;

	if (done)
		*done = 1;
}

/*
 * 同步提交。成功返回 0，失败返回 -1。
 * 调用方负责 bio_put。
 */
int submit_bio_wait(struct bio *bio)
{
	int done = 0;
	bio_end_io_t *saved_end;
	void *saved_priv;

	if (!bio)
		return -1;

	saved_end = bio->bi_end_io;
	saved_priv = bio->bi_private;
	bio->bi_end_io = bio_wait_end_io;
	bio->bi_private = &done;

	submit_bio(bio);

	/* 同步驱动下 end_io 已在 submit_bio 内调用 */
	if (!done && bio->bi_status == BLK_STS_OK) {
		/* 防御未调 end_io：仍以 status 为准 */
		printk(KERN_ERR "block: submit_bio_wait: end_io not called\n");
	}

	bio->bi_end_io = saved_end;
	bio->bi_private = saved_priv;

	if (saved_end)
		saved_end(bio);

	return bio->bi_status == BLK_STS_OK ? 0 : -1;
}

/* 读取一个扇区 */
int blkdev_read_sect(struct gendisk *disk, sector_t sector, void *buf)
{
	struct bio *bio;
	int r;

	if (!disk || !buf)
		return -1;
	bio = bio_alloc();
	if (!bio)
		return -1;
	bio_set_dev(bio, disk);
	bio_set_op_attrs(bio, REQ_OP_READ);
	bio->bi_sector = sector;
	bio->bi_size = BLOCK_SECTOR_SIZE;
	bio->bi_data = buf;
	r = submit_bio_wait(bio);
	bio_put(bio);
	return r;
}

/* 写入一个扇区 */
int blkdev_write_sect(struct gendisk *disk, sector_t sector, const void *buf)
{
	struct bio *bio;
	int r;

	if (!disk || !buf)
		return -1;
	bio = bio_alloc();
	if (!bio)
		return -1;
	bio_set_dev(bio, disk);
	bio_set_op_attrs(bio, REQ_OP_WRITE);
	bio->bi_sector = sector;
	bio->bi_size = BLOCK_SECTOR_SIZE;
	bio->bi_data = (void *)buf;
	r = submit_bio_wait(bio);
	bio_put(bio);
	return r;
}
