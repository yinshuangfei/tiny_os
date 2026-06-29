#ifndef _buf_h_
#define _buf_h_

#include "types.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"

struct buf {
	int valid;		// has data been read from disk?
	int disk;		// does disk "own" buf?
	uint dev;		// 设备号
	uint blockno;		// block 号
	struct sleeplock lock;	// 块锁
	uint refcnt;		// 引用计数
	struct buf *prev;	// LRU cache list
	struct buf *next;	// LRU cache list
	uchar data[BSIZE];	// data
};

#endif /** _buf_h_ */
