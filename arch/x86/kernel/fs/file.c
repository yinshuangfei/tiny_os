/*
 * VFS：打开文件表与进程 fd（对齐 Linux fs/file.c）。
 * 设备号从 f->ip（类 Linux f_inode / i_rdev）读取，不缓存在 struct file。
 */
#include "../types.h"
#include "../defs.h"
#include "../param.h"
#include "../proc.h"
#include "../block/blk.h"
#include "../driver/chrdev.h"
#include "vfs.h"

/* 全局文件描述符表 */
static struct file ftable[NFILE];

void fileinit(void)
{
	int i;

	for (i = 0; i < NFILE; i++)
		ftable[i].type = FD_NONE;
}

/* 分配一个空闲文件描述符 */
struct file *filealloc(void)
{
	int i;

	for (i = 0; i < NFILE; i++) {
		if (ftable[i].ref == 0) {
			ftable[i].ref = 1;
			ftable[i].type = FD_NONE;
			ftable[i].readable = 0;
			ftable[i].writable = 0;
			ftable[i].flags = 0;
			ftable[i].flock = 0;
			ftable[i].ip = 0;
			ftable[i].off = 0;
			return &ftable[i];
		}
	}
	return 0;
}

/* 增加文件描述符引用计数 */
struct file *filedup(struct file *f)
{
	if (!f)
		return 0;
	if (f->ref < 1)
		panic("filedup");
	f->ref++;
	return f;
}

/* 关闭一个文件描述符 */
void fileclose(struct file *f)
{
	if (!f)
		return;
	if (f->ref < 1)
		panic("fileclose");
	f->ref--;
	if (f->ref > 0)
		return;
	/*
	 * 最后一次关闭：释放 flock，唤醒等待者。
	 * 对齐 Linux：锁属于 open file description，全部关闭后释放。
	 */
	if (f->flock && f->ip) {
		f->flock = 0;
		wakeup(f->ip);
	}
	/*
	 * FIFO：先 pipe_release（减 readers/writers），再 iput。
	 * 对齐 Linux f_op->release + fput。
	 */
	if (f->ip && f->ip->type == T_FIFO)
		pipe_release(f);
	if (f->type == FD_INODE || f->type == FD_CHAR || f->type == FD_BLOCK)
		fs_iput(f->ip);
	f->type = FD_NONE;
	f->ip = 0;
	f->flock = 0;
}

/*
 * 块设备按字节偏移读写（内部按扇区；非对齐走 bounce）。
 * 成功返回已传输字节数；失败且无进度返回 -1。
 * do_write: 1 写，0 读。
 * 设备号从 f->ip（类 Linux i_rdev）读取。
 */
static int file_blk_rw(struct file *f, char *buf, int n, int do_write)
{
	struct gendisk *gd;
	uchar sect[BLOCK_SECTOR_SIZE];
	uint64 off;
	int done, chunk, i;
	sector_t sec;
	uint sec_off;

	if (!f || !f->ip || !buf || n < 0)
		return -1;
	gd = blk_lookup_dev(f->ip->rdev);
	if (!gd)
		return -1;

	off = f->off;
	done = 0;
	while (done < n) {
		sec = (sector_t)(off / BLOCK_SECTOR_SIZE);
		sec_off = (uint)(off % BLOCK_SECTOR_SIZE);
		if (sec >= gd->capacity)
			break;

		/* 计算当前扇区剩余字节数 */
		chunk = BLOCK_SECTOR_SIZE - (int)sec_off;
		/* 如果当前扇区剩余字节数大于本次仍需传输的字节数，则取后者 */
		if (chunk > n - done)
			chunk = n - done;

		if (do_write) {
			/*
			 * 写起点不在扇区开头，或写不满一个扇区：
			 * 不能整扇区覆盖，须先读出扇区再改写（RMW）。
			 */
			if (sec_off != 0 || chunk != BLOCK_SECTOR_SIZE) {
				if (blkdev_read_sect(gd, sec, sect) < 0)
					return done ? done : -1;
				for (i = 0; i < chunk; i++)
					sect[sec_off + i] = (uchar)buf[done + i];
				if (blkdev_write_sect(gd, sec, sect) < 0)
					return done ? done : -1;
			} else {
				if (blkdev_write_sect(gd, sec, buf + done) < 0)
					return done ? done : -1;
			}
		} else {
			if (blkdev_read_sect(gd, sec, sect) < 0)
				return done ? done : -1;
			for (i = 0; i < chunk; i++)
				buf[done + i] = (char)sect[sec_off + i];
		}

		done += chunk;
		off += (uint64)chunk;
	}
	f->off = (uint)off;
	return done;
}

/*
 * 字符设备读：MAJOR(rdev) → register_chrdev 注册的 fops。
 */
static int file_char_read(struct file *f, char *dst, int n)
{
	const struct file_operations *fops;

	if (!f->ip)
		return -1;
	fops = chrdev_get(MAJOR(f->ip->rdev));
	if (!fops || !fops->read)
		return -1;
	return fops->read(f, dst, n);
}

/* 字符设备写 */
static int file_char_write(struct file *f, char *src, int n)
{
	const struct file_operations *fops;

	if (!f->ip)
		return -1;
	fops = chrdev_get(MAJOR(f->ip->rdev));
	if (!fops || !fops->write)
		return -1;
	return fops->write(f, src, n);
}

/* 字符设备 ioctl */
int fileioctl(struct file *f, unsigned int req, unsigned int arg)
{
	const struct file_operations *fops;

	if (!f || f->type != FD_CHAR || !f->ip)
		return -1;
	fops = chrdev_get(MAJOR(f->ip->rdev));
	if (!fops || !fops->ioctl)
		return -1;
	return fops->ioctl(f, req, arg);
}

/* 从文件读取数据 */
int fileread(struct file *f, char *dst, int n)
{
	int r;

	if (!f || !f->readable || n < 0)
		return -1;
	if (f->type == FD_BLOCK)
		return file_blk_rw(f, dst, n, 0);
	if (f->type == FD_CHAR)
		return file_char_read(f, dst, n);
	if (f->type == FD_INODE) {
		/* T_FIFO：无文件偏移（pipe 流式缓冲在 i_pipe） */
		if (f->ip && f->ip->type == T_FIFO)
			return fs_readi(f->ip, dst, 0, (uint)n);
		r = fs_readi(f->ip, dst, f->off, (uint)n);
		if (r > 0)
			f->off += r;
		return r;
	}
	return -1;
}

/* 将数据写入文件 */
int filewrite(struct file *f, char *src, int n)
{
	int r;

	if (!f || !f->writable || n < 0)
		return -1;
	if (f->type == FD_BLOCK)
		return file_blk_rw(f, src, n, 1);
	if (f->type == FD_CHAR)
		return file_char_write(f, src, n);
	if (f->type == FD_INODE) {
		if (f->ip && f->ip->type == T_FIFO)
			return fs_writei(f->ip, src, 0, (uint)n);
		/* O_APPEND：每次写前定位到文件末尾 */
		if ((f->flags & O_APPEND) && f->ip && f->ip->type == T_FILE)
			f->off = f->ip->size;
		r = fs_writei(f->ip, src, f->off, (uint)n);
		if (r > 0)
			f->off += r;
		return r;
	}
	return -1;
}

/*
 * 调整文件偏移（对齐 Linux lseek 教学子集）。
 * 成功返回新偏移；管道/字符设备不可定位，返回 -1（类 ESPIPE）。
 */
int filelseek(struct file *f, int offset, int whence)
{
	int newoff;
	uint size;

	if (!f)
		return -1;
	if (f->type == FD_CHAR)
		return -1;
	if (f->type == FD_INODE && f->ip && f->ip->type == T_FIFO)
		return -1;
	if (f->type != FD_INODE && f->type != FD_BLOCK)
		return -1;
	if (!f->ip)
		return -1;

	size = f->ip->size;
	switch (whence) {
	case SEEK_SET:
		newoff = offset;
		break;
	case SEEK_CUR:
		newoff = (int)f->off + offset;
		break;
	case SEEK_END:
		newoff = (int)size + offset;
		break;
	default:
		return -1;
	}
	if (newoff < 0)
		return -1;
	f->off = (uint)newoff;
	return newoff;
}

/*
 * flock：锁挂在 open file description（struct file）上。
 * 冲突按 inode 判定；同一 file*（dup/fork）共享一把锁。
 */
static int flock_conflict(struct file *self, struct inode *ip, int want)
{
	int i;
	struct file *f;

	for (i = 0; i < NFILE; i++) {
		f = &ftable[i];
		if (f->ref < 1 || f == self || f->ip != ip || !f->flock)
			continue;
		if (want == LOCK_EX)
			return 1;
		if (f->flock == LOCK_EX)
			return 1;
	}
	return 0;
}

int fileflock(struct file *f, int op)
{
	int nb, cmd;
	struct inode *ip;

	if (!f || !f->ip)
		return -1;
	if (f->type != FD_INODE && f->type != FD_CHAR && f->type != FD_BLOCK)
		return -1;

	nb = op & LOCK_NB;
	cmd = op & ~LOCK_NB;
	ip = f->ip;

	if (cmd == LOCK_UN) {
		if (f->flock) {
			f->flock = 0;
			wakeup(ip);
		}
		return 0;
	}
	if (cmd != LOCK_SH && cmd != LOCK_EX)
		return -1;

	for (;;) {
		if (!flock_conflict(f, ip, cmd)) {
			f->flock = cmd;
			return 0;
		}
		if (nb)
			return -1;
		sleep(ip);
	}
}

/* 分配一个空闲文件描述符 */
int fdalloc(struct file *f)
{
	return fdalloc_ge(f, 0);
}

/* 分配 >= minfd 的最小空闲 fd（fcntl F_DUPFD） */
int fdalloc_ge(struct file *f, int minfd)
{
	int fd;
	struct proc *p = myproc();

	if (!p || !f || minfd < 0)
		return -1;
	if (minfd > NOFILE)
		minfd = NOFILE;
	for (fd = minfd; fd < NOFILE; fd++) {
		if (p->ofile[fd] == 0) {
			p->ofile[fd] = f;
			p->fdflags[fd] = 0;
			return fd;
		}
	}
	return -1;
}

/* 获取进程的第 fd 个打开文件 */
struct file *fdget(int fd)
{
	struct proc *p = myproc();

	if (!p || fd < 0 || fd >= NOFILE)
		return 0;
	return p->ofile[fd];
}

/* 打开控制台设备文件 */
static struct file *open_console(int readable, int writable)
{
	struct inode *ip;
	struct file *f;

	ip = fs_namei("/dev/console");
	if (!ip)
		return 0;
	f = filealloc();
	if (!f) {
		fs_iput(ip);
		return 0;
	}
	f->type = FD_CHAR;
	f->ip = ip;
	f->readable = readable;
	f->writable = writable;
	if (readable && writable)
		f->flags = O_RDWR;
	else if (writable)
		f->flags = O_WRONLY;
	else
		f->flags = O_RDONLY;
	f->off = 0;
	return f;
}

/* 安装标准输入、输出、错误输出三个文件描述符 */
void fd_install_stdio(struct proc *p)
{
	struct file *in, *out;

	if (!p)
		return;
	in = open_console(1, 0);
	out = open_console(0, 1);
	if (!in || !out) {
		if (in)
			fileclose(in);
		if (out)
			fileclose(out);
		return;
	}
	p->ofile[0] = in;
	p->ofile[1] = out;
	p->ofile[2] = filedup(out);
	p->fdflags[0] = 0;
	p->fdflags[1] = 0;
	p->fdflags[2] = 0;
}

/* 复制进程的打开文件 */
void fd_copy(struct proc *dst, struct proc *src)
{
	int i;

	if (!dst || !src)
		return;
	for (i = 0; i < NOFILE; i++) {
		if (src->ofile[i]) {
			dst->ofile[i] = filedup(src->ofile[i]);
			dst->fdflags[i] = src->fdflags[i];
		} else {
			dst->ofile[i] = 0;
			dst->fdflags[i] = 0;
		}
	}
}

/* 关闭进程的所有打开文件 */
void fd_closeall(struct proc *p)
{
	int i;

	if (!p)
		return;
	for (i = 0; i < NOFILE; i++) {
		if (p->ofile[i]) {
			fileclose(p->ofile[i]);
			p->ofile[i] = 0;
		}
		p->fdflags[i] = 0;
	}
}

/* execve 后关闭带 FD_CLOEXEC 的 fd */
void fd_close_on_exec(struct proc *p)
{
	int i;

	if (!p)
		return;
	for (i = 0; i < NOFILE; i++) {
		if (!p->ofile[i])
			continue;
		if (p->fdflags[i] & FD_CLOEXEC) {
			fileclose(p->ofile[i]);
			p->ofile[i] = 0;
			p->fdflags[i] = 0;
		}
	}
}
