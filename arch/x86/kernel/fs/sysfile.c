/*
 * 文件相关系统调用：open / close / read / write。
 */
#include "../types.h"
#include "../defs.h"
#include "../param.h"
#include "../proc.h"
#include "../syscall.h"
#include "fs.h"

int sys_open(struct trapframe *tf)
{
	char path[NNAME];
	int flags, fd;
	struct inode *ip;
	struct file *f;

	if (argstr(tf, 0, path, NNAME) < 0)
		return -1;
	if (argint(tf, 1, &flags) < 0)
		return -1;

	if (flags & O_CREATE) {
		ip = fs_create(path, T_FILE);
		if (!ip)
			ip = fs_namei(path);
	} else {
		ip = fs_namei(path);
	}
	if (!ip)
		return -1;

	f = filealloc();
	if (!f) {
		fs_iput(ip);
		return -1;
	}
	fd = fdalloc(f);
	if (fd < 0) {
		fileclose(f);
		fs_iput(ip);
		return -1;
	}

	if (ip->type == T_DEV) {
		f->type = FD_DEVICE;
		f->major = ip->major;
	} else {
		f->type = FD_INODE;
	}
	f->ip = ip;
	f->off = 0;
	f->readable = !(flags & O_WRONLY);
	f->writable = (flags & O_WRONLY) || (flags & O_RDWR);
	return fd;
}

int sys_close(struct trapframe *tf)
{
	int fd;
	struct proc *p = myproc();
	struct file *f;

	if (argint(tf, 0, &fd) < 0)
		return -1;
	if (!p || fd < 0 || fd >= NOFILE)
		return -1;
	f = p->ofile[fd];
	if (!f)
		return -1;
	p->ofile[fd] = 0;
	fileclose(f);
	return 0;
}

int sys_read(struct trapframe *tf)
{
	int fd, n;
	uint uaddr;
	struct proc *p = myproc();
	struct file *f;
	char buf[128];
	int r, total, chunk;

	if (argint(tf, 0, &fd) < 0 || argaddr(tf, 1, &uaddr) < 0 ||
	    argint(tf, 2, &n) < 0)
		return -1;
	if (!p || !p->pagetable || n < 0)
		return -1;
	f = fdget(fd);
	if (!f)
		return -1;

	total = 0;
	while (total < n) {
		/* 每次最多读取 sizeof(buf) 个字节 */
		chunk = n - total;
		if (chunk > (int)sizeof(buf))
			chunk = sizeof(buf);
		r = fileread(f, buf, chunk);
		if (r < 0)
			return -1;
		if (r == 0)
			break;
		if (copyout(p->pagetable, uaddr + total, buf, r) < 0)
			return -1;
		total += r;
		if (r < chunk)
			break;
	}
	return total;
}

int sys_write(struct trapframe *tf)
{
	int fd, n;
	uint uaddr;
	struct proc *p = myproc();
	struct file *f;
	char buf[128];
	int r, total, chunk;

	if (argint(tf, 0, &fd) < 0 || argaddr(tf, 1, &uaddr) < 0 ||
	    argint(tf, 2, &n) < 0)
		return -1;
	if (!p || !p->pagetable || n < 0)
		return -1;
	f = fdget(fd);
	if (!f)
		return -1;

	total = 0;
	while (total < n) {
		chunk = n - total;
		if (chunk > (int)sizeof(buf))
			chunk = sizeof(buf);
		if (copyin(p->pagetable, buf, uaddr + total, chunk) < 0)
			return -1;
		r = filewrite(f, buf, chunk);
		if (r < 0)
			return -1;
		if (r == 0)
			break;
		total += r;
		if (r < chunk)
			break;
	}
	return total;
}
