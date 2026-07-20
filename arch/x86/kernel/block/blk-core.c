/*
 * 块层核心：submit_bio（对齐 Linux blk-core submit_bio 简化路径）。
 *
 * Linux 异步队列 + 调度合并；此处 make_request_fn 将 bio 入队并 kick，
 * 完成时调 bi_end_io；submit_bio_wait 睡眠直到 end_io。
 */
#include "blk.h"
#include "../defs.h"
#include "../printk.h"
#include "../proc.h"
#include "../lock/proc_lock.h"

/* 分配一个 request（教学：与 bio 一对一） */
struct request *blk_alloc_request(struct request_queue *q)
{
	struct request *rq;

	rq = kmalloc(sizeof(*rq));
	if (!rq)
		return 0;
	memset(rq, 0, sizeof(*rq));
	INIT_LIST_HEAD(&rq->queuelist);
	rq->q = q;
	rq->error = BLK_STS_OK;
	return rq;
}

/* 释放一个 request */
void blk_free_request(struct request *rq)
{
	if (!rq)
		return;
	kfree(rq);
}

/* 入队；调用方须已持有保护队列的锁（ide_lock 或 queue_lock） */
void blk_queue_push(struct request_queue *q, struct request *rq)
{
	if (!q || !rq)
		return;
	list_add_tail(&rq->queuelist, &q->queue_head);
}

/* 窥视队首，不摘除 */
struct request *blk_queue_peek(struct request_queue *q)
{
	if (!q || list_empty(&q->queue_head))
		return 0;
	return list_first_entry(&q->queue_head, struct request, queuelist);
}

/* 取出队首 */
struct request *blk_queue_pop(struct request_queue *q)
{
	struct request *rq;

	rq = blk_queue_peek(q);
	if (!rq)
		return 0;
	list_del(&rq->queuelist);
	return rq;
}

/* 队列是否为空 */
int blk_queue_empty(struct request_queue *q)
{
	return !q || list_empty(&q->queue_head);
}

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
	 * 教学实现：调驱动挂的 make_request_fn（入队并 kick）。
	 */
	q->make_request_fn(q, bio);
}

/* submit_bio_wait 用的完成同步对象 */
struct bio_waiter {
	struct spinlock lock;
	volatile int done;
};

/* 等待完成回调 */
static void bio_wait_end_io(struct bio *bio)
{
	struct bio_waiter *w = bio->bi_private;

	if (!w)
		return;
	acquire(&w->lock);
	w->done = 1;
	wakeup(w);
	release(&w->lock);
}

static void bio_waiter_sleep(struct bio_waiter *w)
{
	struct proc *p = myproc();

	if (!holding(&w->lock))
		panic("bio_waiter_sleep");
	if (!p) {
		/* 早期 boot（尚无进程上下文）：只能忙等 */
		while (!w->done) {
			release(&w->lock);
			acquire(&w->lock);
		}
		return;
	}

	acquire(&proc_lock);
	release(&w->lock);
	p->chan = w;
	p->wakeup_tick = 0;
	p->state = SLEEPING;
	release(&proc_lock);
	sched();
	acquire(&w->lock);
}

/*
 * 同步提交。成功返回 0，失败返回 -1。
 * 睡眠直到 bi_end_io；调用方负责 bio_put。
 */
int submit_bio_wait(struct bio *bio)
{
	struct bio_waiter wait;
	bio_end_io_t *saved_end;
	void *saved_priv;

	if (!bio)
		return -1;

	initlock(&wait.lock, "biowait");
	wait.done = 0;

	saved_end = bio->bi_end_io;
	saved_priv = bio->bi_private;
	bio->bi_end_io = bio_wait_end_io;
	bio->bi_private = &wait;

	submit_bio(bio);

	acquire(&wait.lock);
	while (!wait.done)
		bio_waiter_sleep(&wait);
	release(&wait.lock);

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
