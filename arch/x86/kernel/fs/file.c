/*
 * 打开文件表与进程 fd 表。
 */
#include "../types.h"
#include "../defs.h"
#include "../param.h"
#include "../proc.h"
#include "fs.h"

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
			ftable[i].ip = 0;
			ftable[i].off = 0;
			ftable[i].major = 0;
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
	if (f->type == FD_INODE || f->type == FD_DEVICE)
		fs_iput(f->ip);
	f->type = FD_NONE;
	f->ip = 0;
}

/* 从文件读取数据 */
int fileread(struct file *f, char *dst, int n)
{
	int r;

	if (!f || !f->readable || n < 0)
		return -1;
	if (f->type == FD_DEVICE) {
		int i;

		if (f->major != DEV_CONSOLE)
			return -1;
		for (i = 0; i < n; i++)
			dst[i] = (char)uart_getc();
		return n;
	}
	if (f->type == FD_INODE) {
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
	if (f->type == FD_DEVICE) {
		int i;

		if (f->major != DEV_CONSOLE)
			return -1;
		for (i = 0; i < n; i++)
			uart_putc(src[i]);
		return n;
	}
	if (f->type == FD_INODE) {
		r = fs_writei(f->ip, src, f->off, (uint)n);
		if (r > 0)
			f->off += r;
		return r;
	}
	return -1;
}

/* 分配一个空闲文件描述符 */
int fdalloc(struct file *f)
{
	int fd;
	struct proc *p = myproc();

	if (!p || !f)
		return -1;
	for (fd = 0; fd < NOFILE; fd++) {
		if (p->ofile[fd] == 0) {
			p->ofile[fd] = f;
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
	f->type = FD_DEVICE;
	f->ip = ip;
	f->major = ip->major;
	f->readable = readable;
	f->writable = writable;
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
}

/* 复制进程的打开文件 */
void fd_copy(struct proc *dst, struct proc *src)
{
	int i;

	if (!dst || !src)
		return;
	for (i = 0; i < NOFILE; i++) {
		if (src->ofile[i])
			dst->ofile[i] = filedup(src->ofile[i]);
		else
			dst->ofile[i] = 0;
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
	}
}
