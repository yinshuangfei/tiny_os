/*
 * bio：块 I/O 描述符（对齐 Linux struct bio 教学子集）。
 * 连续内核缓冲，无 scatter-gather / bvec。
 */
#include "blk.h"
#include "../defs.h"

/* 分配一个 bio 结构体 */
struct bio *bio_alloc(void)
{
	struct bio *bio;

	bio = kmalloc(sizeof(*bio));
	if (!bio)
		return 0;
	memset(bio, 0, sizeof(*bio));
	bio->bi_status = BLK_STS_OK;
	return bio;
}

/* 释放一个 bio 结构体 */
void bio_put(struct bio *bio)
{
	if (!bio)
		return;
	kfree(bio);
}

/* 设置通用磁盘 */
void bio_set_dev(struct bio *bio, struct gendisk *disk)
{
	if (bio)
		bio->bi_disk = disk;
}

/* 设置操作和属性 */
void bio_set_op_attrs(struct bio *bio, unsigned int op)
{
	if (bio)
		bio->bi_opf = op & REQ_OP_MASK;
}
