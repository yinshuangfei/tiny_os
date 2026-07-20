/*
 * 文件相关系统调用（VFS 调用方）：open / close / read / write 等。
 * 路径与读写经 VFS（namei / file / inode），不直接碰 ramfs。
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

	if (ip->type == T_CHAR)
		f->type = FD_CHAR;
	else if (ip->type == T_BLK)
		f->type = FD_BLOCK;
	else
		f->type = FD_INODE;
	f->ip = ip;
	f->off = 0;
	f->readable = !(flags & O_WRONLY);
	f->writable = (flags & O_WRONLY) || (flags & O_RDWR);
	return fd;
}

static unsigned short inode_to_mode(short type)
{
	switch (type) {
	case T_DIR:
		return S_IFDIR;
	case T_FILE:
		return S_IFREG;
	case T_CHAR:
		return S_IFCHR;
	case T_BLK:
		return S_IFBLK;
	case T_FIFO:
		return S_IFIFO;
	default:
		return 0;
	}
}

/* 由 inode 填充用户态 struct stat */
static void inode_to_stat(struct inode *ip, struct stat *st)
{
	st->st_mode = inode_to_mode(ip->type);
	st->st_ino = ip->inum;
	st->st_size = ip->size;
}

/* fstat(fd, &st)：已打开文件 */
int sys_fstat(struct trapframe *tf)
{
	int fd;
	uint uaddr;
	struct proc *p = myproc();
	struct file *f;
	struct stat st;

	if (argint(tf, 0, &fd) < 0 || argaddr(tf, 1, &uaddr) < 0)
		return -1;
	if (!p || !p->pagetable)
		return -1;
	f = fdget(fd);
	if (!f || !f->ip)
		return -1;

	inode_to_stat(f->ip, &st);
	if (copyout(p->pagetable, uaddr, &st, sizeof(st)) < 0)
		return -1;
	return 0;
}

/* stat(path, &st)：按路径查询，无需 open */
int sys_stat(struct trapframe *tf)
{
	char path[NNAME];
	uint uaddr;
	struct proc *p = myproc();
	struct inode *ip;
	struct stat st;

	if (argstr(tf, 0, path, NNAME) < 0 || argaddr(tf, 1, &uaddr) < 0)
		return -1;
	if (!p || !p->pagetable)
		return -1;

	ip = fs_namei(path);
	if (!ip)
		return -1;

	inode_to_stat(ip, &st);
	fs_iput(ip);
	if (copyout(p->pagetable, uaddr, &st, sizeof(st)) < 0)
		return -1;
	return 0;
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

/* ioctl(fd, request, arg)：目前仅 /dev/console 的 TCGETS/TCSETS */
int sys_ioctl(struct trapframe *tf)
{
	int fd, req;
	uint arg;
	struct file *f;

	if (argint(tf, 0, &fd) < 0)
		return -1;
	if (argint(tf, 1, &req) < 0)
		return -1;
	if (argaddr(tf, 2, &arg) < 0)
		return -1;
	f = fdget(fd);
	if (!f)
		return -1;
	return fileioctl(f, (unsigned int)req, arg);
}

/* dup(oldfd)：复制到最小空闲 fd（对齐 Linux dup） */
int sys_dup(struct trapframe *tf)
{
	int fd, nfd;
	struct file *f;

	if (argint(tf, 0, &fd) < 0)
		return -1;
	f = fdget(fd);
	if (!f)
		return -1;
	nfd = fdalloc(f);
	if (nfd < 0)
		return -1;
	filedup(f);
	return nfd;
}

/* lseek(fd, offset, whence)：成功返回新偏移 */
int sys_lseek(struct trapframe *tf)
{
	int fd, offset, whence;
	struct file *f;

	if (argint(tf, 0, &fd) < 0 || argint(tf, 1, &offset) < 0 ||
	    argint(tf, 2, &whence) < 0)
		return -1;
	f = fdget(fd);
	if (!f)
		return -1;
	return filelseek(f, offset, whence);
}

/* pipe(fd[2])：fd[0] 读端，fd[1] 写端 */
int sys_pipe(struct trapframe *tf)
{
	uint uaddr;
	struct proc *p = myproc();
	struct file *rf, *wf;
	int fd0, fd1;

	if (argaddr(tf, 0, &uaddr) < 0)
		return -1;
	if (!p || !p->pagetable)
		return -1;
	if (pipealloc(&rf, &wf) < 0)
		return -1;
	fd0 = -1;
	if ((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0) {
		if (fd0 >= 0)
			p->ofile[fd0] = 0;
		fileclose(rf);
		fileclose(wf);
		return -1;
	}
	if (copyout(p->pagetable, uaddr, &fd0, sizeof(fd0)) < 0 ||
	    copyout(p->pagetable, uaddr + sizeof(fd0), &fd1, sizeof(fd1)) < 0) {
		p->ofile[fd0] = 0;
		p->ofile[fd1] = 0;
		fileclose(rf);
		fileclose(wf);
		return -1;
	}
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
		/* 管道：一旦读到数据即返回（与 Linux 短读语义一致） */
		if (r < chunk || (f->ip && f->ip->type == T_FIFO))
			break;
	}
	return total;
}

int sys_chdir(struct trapframe *tf)
{
	char path[NNAME];
	struct inode *ip;
	struct proc *p = myproc();

	if (!p)
		return -1;
	if (argstr(tf, 0, path, NNAME) < 0)
		return -1;
	ip = fs_namei(path);
	if (!ip)
		return -1;
	if (ip->type != T_DIR) {
		fs_iput(ip);
		return -1;
	}
	if (p->cwd)
		fs_iput(p->cwd);
	p->cwd = ip;
	return 0;
}

/* mkdir(path, mode)：mode 暂忽略（ramfs 无权限位） */
int sys_mkdir(struct trapframe *tf)
{
	char path[NNAME];
	int mode;

	if (argstr(tf, 0, path, NNAME) < 0)
		return -1;
	if (argint(tf, 1, &mode) < 0)
		return -1;
	(void)mode;
	return fs_mkdir(path);
}

int sys_rmdir(struct trapframe *tf)
{
	char path[NNAME];

	if (argstr(tf, 0, path, NNAME) < 0)
		return -1;
	return fs_rmdir(path);
}

/* link(oldpath, newpath) */
int sys_link(struct trapframe *tf)
{
	char oldpath[NNAME], newpath[NNAME];

	if (argstr(tf, 0, oldpath, NNAME) < 0 ||
	    argstr(tf, 1, newpath, NNAME) < 0)
		return -1;
	return fs_link(oldpath, newpath);
}

/* unlink(path) */
int sys_unlink(struct trapframe *tf)
{
	char path[NNAME];

	if (argstr(tf, 0, path, NNAME) < 0)
		return -1;
	return fs_unlink(path);
}

/* rename(oldpath, newpath) */
int sys_rename(struct trapframe *tf)
{
	char oldpath[NNAME], newpath[NNAME];

	if (argstr(tf, 0, oldpath, NNAME) < 0 ||
	    argstr(tf, 1, newpath, NNAME) < 0)
		return -1;
	return fs_rename(oldpath, newpath);
}

/* getcwd(buf, size)：成功返回写入长度（含 '\0'），失败 -1 */
int sys_getcwd(struct trapframe *tf)
{
	uint uaddr;
	int size;
	struct proc *p = myproc();
	char buf[NNAME];
	int n;

	if (!p || !p->pagetable)
		return -1;
	if (argaddr(tf, 0, &uaddr) < 0 || argint(tf, 1, &size) < 0)
		return -1;
	if (size < 2)
		return -1;
	/* 内核栈上最多拼 NNAME；用户 size 更小时按 size 截断判断 */
	n = fs_getcwd(buf, size < NNAME ? size : NNAME);
	if (n < 0)
		return -1;
	if (copyout(p->pagetable, uaddr, buf, n) < 0)
		return -1;
	return n;
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
