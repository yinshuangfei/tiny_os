/*
 * 匿名管道（对齐 Linux pipefs / pipe_inode_info 教学子集）。
 *
 * Linux：两端 struct file 共享同一 inode（S_IFIFO），缓冲在 inode->i_pipe；
 * readers/writers 计打开端数量；无读者写 → -EPIPE + SIGPIPE。
 * 本实现：T_FIFO inode + i_pipe，FD_INODE，无独立 FD_PIPE。
 */
#include "../types.h"
#include "../defs.h"
#include "../param.h"
#include "../proc.h"
#include "../lock/spinlock.h"
#include "../lock/proc_lock.h"
#include "../mm/slab.h"
#include "../ipc/signal.h"
#include "vfs.h"

#define PIPESIZE	512
#define NPIPE		32	/* 同时存在的匿名 pipe inode 上限 */

struct pipe {
	struct spinlock lock;
	char data[PIPESIZE];
	uint nread;		/* 读取位置 */
	uint nwrite;		/* 写入位置 */
	uint readers;		/* 读端 struct file 数（类 Linux） */
	uint writers;		/* 写端 struct file 数 */
};

/* 全局匿名管道 inode 数组 */
static struct inode pipe_inodes[NPIPE];
static uint pipe_next_inum = 1;

static int pipe_read(struct inode *ip, char *dst, uint off, uint n);
static int pipe_write(struct inode *ip, char *src, uint off, uint n);
static void pipe_evict(struct inode *ip);

/* 匿名管道 inode 操作 */
static const struct inode_operations pipe_iops = {
	.lookup = 0,
	.create = 0,
	.mkdir = 0,
	.rmdir = 0,
	.mknod = 0,
	.get_name = 0,
	.evict = pipe_evict,
	.read = pipe_read,
	.write = pipe_write,
};

static void sleep_chan(void *chan, struct spinlock *lk)
{
	struct proc *p = myproc();

	if (!holding(lk))
		panic("pipe sleep_chan");
	if (!chan)
		panic("pipe sleep_chan: null");

	acquire(&proc_lock);
	release(lk);
	p->chan = chan;
	p->wakeup_tick = 0;
	p->state = SLEEPING;
	release(&proc_lock);
	sched();
	acquire(lk);
}

/* 分配一个匿名管道 inode */
static struct inode *pipe_inode_alloc(struct pipe *pi)
{
	int i;
	struct inode *ip;

	for (i = 0; i < NPIPE; i++) {
		ip = &pipe_inodes[i];
		if (ip->type == 0) {
			ip->inum = pipe_next_inum++;
			if (pipe_next_inum == 0)
				pipe_next_inum = 1;
			ip->type = T_FIFO;
			ip->rdev = 0;
			ip->ref = 1;
			ip->size = 0;
			ip->data = 0;
			ip->dents = 0;
			ip->parent = 0;
			ip->i_op = &pipe_iops;
			ip->i_pipe = pi;
			return ip;
		}
	}
	return 0;
}

/* 匿名管道 inode 释放 */
static void pipe_evict(struct inode *ip)
{
	if (!ip)
		return;
	if (ip->i_pipe) {
		kfree(ip->i_pipe);
		ip->i_pipe = 0;
	}
	ip->type = 0;
	ip->i_op = 0;
	ip->size = 0;
	ip->inum = 0;
}

/*
 * 最后一个指向该 file 的 fd 关闭时调用（类 Linux pipe_release）。
 * 须在 fs_iput 之前调用；pipe 内存在 inode evict 时释放。
 */
void pipe_release(struct file *f)
{
	struct pipe *pi;

	if (!f || !f->ip || f->ip->type != T_FIFO || !f->ip->i_pipe)
		return;
	pi = f->ip->i_pipe;

	acquire(&pi->lock);
	if (f->readable) {
		if (pi->readers > 0)
			pi->readers--;
		if (pi->readers == 0)
			wakeup(&pi->nwrite);
	}
	if (f->writable) {
		if (pi->writers > 0)
			pi->writers--;
		if (pi->writers == 0)
			wakeup(&pi->nread);
	}
	release(&pi->lock);
}

static int pipe_write(struct inode *ip, char *src, uint off, uint n)
{
	int i;
	struct pipe *pi;
	struct proc *pr = myproc();

	(void)off;
	if (!ip || !ip->i_pipe || !src || (int)n < 0)
		return -1;
	pi = ip->i_pipe;

	acquire(&pi->lock);

	/* 无读者：对齐 Linux -EPIPE + SIGPIPE */
	if (pi->readers == 0) {
		release(&pi->lock);
		if (pr)
			signal_send(pr->pid, SIGPIPE);
		return -1;
	}

	/*
	 * 写入量 <= PIPE_BUF(PIPESIZE) 时原子写入：等够空位再一次性写入，
	 * 避免与其它写端字节交错（Linux PIPE_BUF 语义教学版）。
	 */
	if (n > 0 && n <= PIPESIZE) {
		while (pi->nwrite + n > pi->nread + PIPESIZE) {
			if (pi->readers == 0 || (pr && pr->killed)) {
				int noread = (pi->readers == 0);

				release(&pi->lock);
				if (noread && pr)
					signal_send(pr->pid, SIGPIPE);
				return -1;
			}
			wakeup(&pi->nread);
			sleep_chan(&pi->nwrite, &pi->lock);
		}
		for (i = 0; i < (int)n; i++)
			pi->data[pi->nwrite++ % PIPESIZE] = src[i];
		wakeup(&pi->nread);
		release(&pi->lock);
		return i;
	}

	for (i = 0; i < (int)n; i++) {
		while (pi->nwrite == pi->nread + PIPESIZE) {
			if (pi->readers == 0 || (pr && pr->killed)) {
				int noread = (pi->readers == 0);

				release(&pi->lock);
				if (noread && pr)
					signal_send(pr->pid, SIGPIPE);
				return i > 0 ? i : -1;
			}
			wakeup(&pi->nread);
			sleep_chan(&pi->nwrite, &pi->lock);
		}
		pi->data[pi->nwrite++ % PIPESIZE] = src[i];
	}
	wakeup(&pi->nread);
	release(&pi->lock);
	return i;
}

static int pipe_read(struct inode *ip, char *dst, uint off, uint n)
{
	int i;
	struct pipe *pi;
	struct proc *pr = myproc();

	(void)off;
	if (!ip || !ip->i_pipe || !dst || (int)n < 0)
		return -1;
	pi = ip->i_pipe;

	acquire(&pi->lock);
	while (pi->nread == pi->nwrite && pi->writers) {
		if (pr && pr->killed) {
			release(&pi->lock);
			return -1;
		}
		sleep_chan(&pi->nread, &pi->lock);
	}
	for (i = 0; i < (int)n; i++) {
		if (pi->nread == pi->nwrite)
			break;
		dst[i] = pi->data[pi->nread++ % PIPESIZE];
	}
	wakeup(&pi->nwrite);
	release(&pi->lock);
	return i;
}

/* 分配一个匿名管道 */
int pipealloc(struct file **f0, struct file **f1)
{
	struct pipe *pi;
	struct inode *ip;

	*f0 = *f1 = 0;
	pi = 0;
	ip = 0;

	if ((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)
		goto bad;
	pi = (struct pipe *)kmalloc(sizeof(*pi));
	if (!pi)
		goto bad;

	pi->readers = 1;
	pi->writers = 1;
	pi->nread = 0;
	pi->nwrite = 0;
	initlock(&pi->lock, "pipe");

	ip = pipe_inode_alloc(pi);
	if (!ip)
		goto bad;

	/* 两端共享同一 inode（再持一份引用） */
	fs_idup(ip);

	(*f0)->type = FD_INODE;
	(*f0)->readable = 1;
	(*f0)->writable = 0;
	(*f0)->ip = ip;
	(*f0)->off = 0;

	(*f1)->type = FD_INODE;
	(*f1)->readable = 0;
	(*f1)->writable = 1;
	(*f1)->ip = ip;
	(*f1)->off = 0;
	return 0;

bad:
	if (ip) {
		ip->i_pipe = 0;
		pipe_evict(ip);
	}
	if (pi)
		kfree(pi);
	if (*f0)
		fileclose(*f0);
	if (*f1)
		fileclose(*f1);
	*f0 = *f1 = 0;
	return -1;
}
