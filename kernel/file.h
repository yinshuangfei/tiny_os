#include "types.h"
#include "fs.h"
#include "sleeplock.h"

struct file {
	enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
	int ref; // reference count
	char readable;
	char writable;
	// struct pipe *pipe; // FD_PIPE
	struct inode *ip;  // FD_INODE and FD_DEVICE
	uint off;          // FD_INODE
	short major;       // FD_DEVICE
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define mkdev(m,n)  ((uint)((m)<<16| (n)))

// in-memory copy of an inode
struct inode {
	uint dev;		// Device number, 设备号
	uint inum;		// Inode number, inode 号
	int ref;		// Reference count, 引用计数
	struct sleeplock lock;	// protects everything below here
	int valid;		// inode has been read from disk?

	short type;		// copy of disk inode
	short major;		// 主设备号
	short minor;		// 次设备号
	short nlink;		// 链接数
	uint size;
	uint addrs[NDIRECT+1];
};

// map major device number to device functions.
struct devsw {
	int (*read)(int, uint64, int);
	int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1