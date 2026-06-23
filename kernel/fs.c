// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "fs.h"
#include "defs.h"
#include "spinlock.h"
#include "param.h"
#include "file.h"
#include "buf.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
// there should be one superblock per disk device, but we run with
// only one device
struct superblock sb;

// Read the super block.
static void readsb(int dev, struct superblock *sb)
{
	struct buf *bp;

	bp = bread(dev, 1);
	memmove(sb, bp->data, sizeof(*sb));
	brelse(bp);
}

// Init fs
void fsinit(int dev)
{
	readsb(dev, &sb);
	if (sb.magic != FSMAGIC)
		panic("invalid file system");
	// initlog(dev, &sb);
}

struct {
	struct spinlock lock;
	struct inode inode[NINODE];
} icache;

void iinit()
{
	int i = 0;

	initlock(&icache.lock, "icache");
	for (i = 0; i < NINODE; i++) {
		// initsleeplock(&icache.inode[i].lock, "inode");
	}
}
