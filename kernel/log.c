#include "spinlock.h"
#include "param.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// Simple logging that allows concurrent FS system calls.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
//
// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
struct logheader {
	int n;
	int block[LOGSIZE];
};

struct log {
	struct spinlock lock;
	int start;
	int size;
	int outstanding; // how many FS sys calls are executing.
	int committing;  // in commit(), please wait.
	int dev;
	struct logheader lh;
};
struct log log;

// Copy modified blocks from cache to log.
// static void write_log(void)
// {
//   int tail;

//   for (tail = 0; tail < log.lh.n; tail++) {
//     struct buf *to = bread(log.dev, log.start+tail+1); // log block
//     struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block
//     memmove(to->data, from->data, BSIZE);
//     bwrite(to);  // write the log
//     brelse(from);
//     brelse(to);
//   }
// }

// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache by increasing refcnt.
// commit()/write_log() will do the disk write.
//
// log_write() replaces bwrite(); a typical use is:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
void log_write(struct buf *b)
{
  int i;

  if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
    panic("too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of trans");

  acquire(&log.lock);
  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno)   // log absorbtion
      break;
  }
  log.lh.block[i] = b->blockno;
  if (i == log.lh.n) {  // Add new block to log?
    bpin(b);
    log.lh.n++;
  }
  release(&log.lock);
}
