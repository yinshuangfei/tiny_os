/*
 * 文件相关系统调用（VFS 调用方）：open / close / read / write 等。
 * 路径与读写经 VFS（namei / file / inode），不直接碰 ramfs。
 */
#include "../types.h"
#include "../defs.h"
#include "../param.h"
#include "../proc.h"
#include "../syscall.h"
#include "../timer.h"
#include "fs.h"
#include "mount.h"

int sys_open(struct trapframe *tf)
{
	char path[NNAME];
	int flags, fd;
	struct inode *ip;
	struct file *f;
	int created;

	if (argstr(tf, 0, path, NNAME) < 0)
		return -1;
	if (argint(tf, 1, &flags) < 0)
		return -1;

	created = 0;
	if (flags & O_CREATE) {
		ip = fs_create(path, T_FILE);
		if (ip) {
			created = 1;
		} else if (flags & O_EXCL) {
			return -1;	/* 已存在或无法创建 */
		} else {
			ip = fs_namei(path);
		}
	} else {
		ip = fs_namei(path);
	}
	if (!ip)
		return -1;

	/* O_TRUNC：可写打开时经 i_op->truncate 截断；无 truncate 的后端对已有文件失败 */
	if ((flags & O_TRUNC) && (flags & (O_WRONLY | O_RDWR)) &&
	    ip->type == T_FILE) {
		if (fs_truncate(ip, 0) < 0 && !created) {
			/* 无 truncate 的后端（如只读 ext2）：打开失败 */
			fs_iput(ip);
			return -1;
		}
	}

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
	/* 保留访问模式与状态位；O_CREATE 等创建位不进入 f_flags */
	f->flags = flags & (O_ACCMODE | O_APPEND | O_NONBLOCK);

	if (ip->type == T_FIFO)
		fifo_on_open(ip, f->readable, f->writable);

	return fd;
}

static unsigned short inode_to_mode(struct inode *ip)
{
	unsigned short t;

	switch (ip->type) {
	case T_DIR:
		t = S_IFDIR;
		break;
	case T_FILE:
		t = S_IFREG;
		break;
	case T_CHAR:
		t = S_IFCHR;
		break;
	case T_BLK:
		t = S_IFBLK;
		break;
	case T_FIFO:
		t = S_IFIFO;
		break;
	case T_LNK:
		t = S_IFLNK;
		break;
	default:
		t = 0;
		break;
	}
	return t | (ip->mode & 07777);
}

/* 由 inode 填充用户态 struct stat */
static void inode_to_stat(struct inode *ip, struct stat *st)
{
	st->st_mode = inode_to_mode(ip);
	st->st_nlink = (unsigned short)ip->nlink;
	st->st_uid = ip->uid;
	st->st_gid = ip->gid;
	st->st_ino = ip->inum;
	st->st_size = ip->size;
	st->st_rdev = ip->rdev;
	st->st_atime = ip->atime;
	st->st_mtime = ip->mtime;
	st->st_ctime = ip->ctime;
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
	if (!p || !proc_pagetable(p))
		return -1;
	f = fdget(fd);
	if (!f || !f->ip)
		return -1;

	inode_to_stat(f->ip, &st);
	if (copyout(proc_pagetable(p), uaddr, &st, sizeof(st)) < 0)
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
	if (!p || !proc_pagetable(p))
		return -1;

	ip = fs_namei(path);
	if (!ip)
		return -1;

	inode_to_stat(ip, &st);
	fs_iput(ip);
	if (copyout(proc_pagetable(p), uaddr, &st, sizeof(st)) < 0)
		return -1;
	return 0;
}

/*
 * readlink(path, buf, bufsiz)：读取符号链接目标（不跟随）。
 * 成功返回写入字节数（不含 '\0'），对齐 Linux readlink(2)。
 */
int sys_readlink(struct trapframe *tf)
{
	char path[NNAME];
	char kbuf[DIRSIZ];
	uint uaddr;
	int bufsiz, n;
	struct proc *p = myproc();
	struct inode *ip;

	if (argstr(tf, 0, path, NNAME) < 0 || argaddr(tf, 1, &uaddr) < 0 ||
	    argint(tf, 2, &bufsiz) < 0)
		return -1;
	if (!p || !proc_pagetable(p) || bufsiz <= 0)
		return -1;

	ip = fs_namei_nofollow(path);
	if (!ip)
		return -1;
	if (ip->type != T_LNK) {
		fs_iput(ip);
		return -1;
	}

	n = fs_readi(ip, kbuf, 0, DIRSIZ);
	fs_iput(ip);
	if (n < 0)
		return -1;
	if (n > bufsiz)
		n = bufsiz;
	if (copyout(proc_pagetable(p), uaddr, kbuf, n) < 0)
		return -1;
	return n;
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
	p->fdflags[fd] = 0;
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

/*
 * fcntl(fd, cmd, arg)：教学子集。
 * F_DUPFD / F_DUPFD_CLOEXEC / F_GETFD / F_SETFD / F_GETFL / F_SETFL
 */
int sys_fcntl(struct trapframe *tf)
{
	int fd, cmd, arg, nfd;
	struct proc *p = myproc();
	struct file *f;

	if (argint(tf, 0, &fd) < 0 || argint(tf, 1, &cmd) < 0)
		return -1;
	if (argint(tf, 2, &arg) < 0)
		return -1;
	if (!p || fd < 0 || fd >= NOFILE)
		return -1;
	f = p->ofile[fd];
	if (!f)
		return -1;

	switch (cmd) {
	case F_DUPFD:
	case F_DUPFD_CLOEXEC:
		if (arg < 0)
			return -1;
		nfd = fdalloc_ge(f, arg);
		if (nfd < 0)
			return -1;
		filedup(f);
		if (cmd == F_DUPFD_CLOEXEC)
			p->fdflags[nfd] = FD_CLOEXEC;
		return nfd;

	case F_GETFD:
		return (int)p->fdflags[fd];

	case F_SETFD:
		p->fdflags[fd] = (unsigned char)(arg & FD_CLOEXEC);
		return 0;

	case F_GETFL:
		return f->flags;

	case F_SETFL:
		/* 访问模式不可改；仅允许状态位 */
		f->flags = (f->flags & O_ACCMODE) |
			   (arg & (O_APPEND | O_NONBLOCK));
		return 0;

	default:
		return -1;
	}
}

/* flock(fd, operation)：对齐 Linux flock(2) */
int sys_flock(struct trapframe *tf)
{
	int fd, op;
	struct file *f;

	if (argint(tf, 0, &fd) < 0 || argint(tf, 1, &op) < 0)
		return -1;
	f = fdget(fd);
	if (!f)
		return -1;
	return fileflock(f, op);
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
	if (!p || !proc_pagetable(p))
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
	if (copyout(proc_pagetable(p), uaddr, &fd0, sizeof(fd0)) < 0 ||
	    copyout(proc_pagetable(p), uaddr + sizeof(fd0), &fd1, sizeof(fd1)) < 0) {
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
	if (!p || !proc_pagetable(p) || n < 0)
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
		if (copyout(proc_pagetable(p), uaddr + total, buf, r) < 0)
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

/* mkdir(path, mode)：mode 暂忽略（默认 VFS_DEF_DIR_MODE；可用 chmod 修改） */
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

/* symlink(target, linkpath)：对齐 Linux symlink(2) */
int sys_symlink(struct trapframe *tf)
{
	char target[NNAME], linkpath[NNAME];

	if (argstr(tf, 0, target, NNAME) < 0 ||
	    argstr(tf, 1, linkpath, NNAME) < 0)
		return -1;
	return fs_symlink(target, linkpath);
}

/*
 * mount(source, target, filesystemtype, mountflags, data)
 * 对齐 Linux mount(2)；flags/data 暂忽略，filesystemtype 可为 NULL。
 */
int sys_mount(struct trapframe *tf)
{
	char source[NNAME], target[NNAME], fstype[NNAME];
	const char *fstype_p;
	uint fstype_addr;
	int flags;

	if (argstr(tf, 0, source, NNAME) < 0 ||
	    argstr(tf, 1, target, NNAME) < 0)
		return -1;
	if (argaddr(tf, 2, &fstype_addr) < 0)
		return -1;
	fstype_p = 0;
	if (fstype_addr) {
		if (argstr(tf, 2, fstype, NNAME) < 0)
			return -1;
		fstype_p = fstype;
	}
	if (argint(tf, 3, &flags) < 0)
		return -1;
	(void)flags;
	/* arg4 data：暂未使用 */
	return do_mount(source, target, fstype_p);
}

/* umount(target)：对齐 Linux umount(2) */
int sys_umount(struct trapframe *tf)
{
	char target[NNAME];

	if (argstr(tf, 0, target, NNAME) < 0)
		return -1;
	return do_umount(target);
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

	if (!p || !proc_pagetable(p))
		return -1;
	if (argaddr(tf, 0, &uaddr) < 0 || argint(tf, 1, &size) < 0)
		return -1;
	if (size < 2)
		return -1;
	/* 内核栈上最多拼 NNAME；用户 size 更小时按 size 截断判断 */
	n = fs_getcwd(buf, size < NNAME ? size : NNAME);
	if (n < 0)
		return -1;
	if (copyout(proc_pagetable(p), uaddr, buf, n) < 0)
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
	if (!p || !proc_pagetable(p) || n < 0)
		return -1;
	f = fdget(fd);
	if (!f)
		return -1;

	total = 0;
	while (total < n) {
		chunk = n - total;
		if (chunk > (int)sizeof(buf))
			chunk = sizeof(buf);
		if (copyin(proc_pagetable(p), buf, uaddr + total, chunk) < 0)
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

/* lstat(path, &st)：不跟随符号链接 */
int sys_lstat(struct trapframe *tf)
{
	char path[NNAME];
	uint uaddr;
	struct proc *p = myproc();
	struct inode *ip;
	struct stat st;

	if (argstr(tf, 0, path, NNAME) < 0 || argaddr(tf, 1, &uaddr) < 0)
		return -1;
	if (!p || !proc_pagetable(p))
		return -1;

	ip = fs_namei_nofollow(path);
	if (!ip)
		return -1;

	inode_to_stat(ip, &st);
	fs_iput(ip);
	if (copyout(proc_pagetable(p), uaddr, &st, sizeof(st)) < 0)
		return -1;
	return 0;
}

/* dup2(oldfd, newfd) */
int sys_dup2(struct trapframe *tf)
{
	int oldfd, newfd;
	struct proc *p = myproc();
	struct file *f;

	if (argint(tf, 0, &oldfd) < 0 || argint(tf, 1, &newfd) < 0)
		return -1;
	if (!p || oldfd < 0 || oldfd >= NOFILE || newfd < 0 || newfd >= NOFILE)
		return -1;
	f = p->ofile[oldfd];
	if (!f)
		return -1;
	if (oldfd == newfd)
		return newfd;
	if (p->ofile[newfd]) {
		fileclose(p->ofile[newfd]);
		p->ofile[newfd] = 0;
		p->fdflags[newfd] = 0;
	}
	p->ofile[newfd] = filedup(f);
	p->fdflags[newfd] = 0;
	return newfd;
}

/* access(path, amode) */
int sys_access(struct trapframe *tf)
{
	char path[NNAME];
	int amode;

	if (argstr(tf, 0, path, NNAME) < 0 || argint(tf, 1, &amode) < 0)
		return -1;
	return fs_access(path, amode);
}

/* truncate(path, length) */
int sys_truncate(struct trapframe *tf)
{
	char path[NNAME];
	int length;
	struct inode *ip;

	if (argstr(tf, 0, path, NNAME) < 0 || argint(tf, 1, &length) < 0)
		return -1;
	if (length < 0)
		return -1;
	ip = fs_namei(path);
	if (!ip)
		return -1;
	if (fs_truncate(ip, (uint)length) < 0) {
		fs_iput(ip);
		return -1;
	}
	fs_iput(ip);
	return 0;
}

/* ftruncate(fd, length) */
int sys_ftruncate(struct trapframe *tf)
{
	int fd, length;
	struct file *f;

	if (argint(tf, 0, &fd) < 0 || argint(tf, 1, &length) < 0)
		return -1;
	if (length < 0)
		return -1;
	f = fdget(fd);
	if (!f || !f->ip || !f->writable)
		return -1;
	return fs_truncate(f->ip, (uint)length);
}

/* fchdir(fd) */
int sys_fchdir(struct trapframe *tf)
{
	int fd;
	struct proc *p = myproc();
	struct file *f;
	struct inode *ip;

	if (!p || argint(tf, 0, &fd) < 0)
		return -1;
	f = fdget(fd);
	if (!f || !f->ip)
		return -1;
	ip = f->ip;
	if (ip->type != T_DIR)
		return -1;
	fs_idup(ip);
	if (p->cwd)
		fs_iput(p->cwd);
	p->cwd = ip;
	return 0;
}

/*
 * mknod(path, mode, dev)：
 * mode 含 S_IF* 类型；dev 为 makedev(major,minor)，FIFO 时忽略。
 */
int sys_mknod(struct trapframe *tf)
{
	char path[NNAME];
	int mode, dev;
	short type;
	unsigned int major, minor;
	struct inode *ip;

	if (argstr(tf, 0, path, NNAME) < 0 ||
	    argint(tf, 1, &mode) < 0 || argint(tf, 2, &dev) < 0)
		return -1;

	switch (mode & S_IFMT) {
	case S_IFCHR:
		type = T_CHAR;
		break;
	case S_IFBLK:
		type = T_BLK;
		break;
	case S_IFIFO:
		type = T_FIFO;
		break;
	default:
		return -1;
	}
	major = ((unsigned int)dev >> 8) & 0xfff;
	minor = (unsigned int)dev & 0xff;
	ip = fs_mknod(path, type, major, minor);
	if (!ip)
		return -1;
	ip->mode = (uint)mode & 07777;
	fs_iput(ip);
	return 0;
}

/* creat(path, mode) ≡ open(O_CREAT|O_WRONLY|O_TRUNC) + chmod */
int sys_creat(struct trapframe *tf)
{
	char path[NNAME];
	int mode;
	struct inode *ip;
	struct file *f;
	int fd;

	if (argstr(tf, 0, path, NNAME) < 0 || argint(tf, 1, &mode) < 0)
		return -1;

	ip = fs_create(path, T_FILE);
	if (!ip) {
		/* 已存在：截断 */
		ip = fs_namei(path);
		if (!ip)
			return -1;
		if (fs_truncate(ip, 0) < 0) {
			fs_iput(ip);
			return -1;
		}
	}
	ip->mode = (uint)mode & 07777;

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
	f->type = FD_INODE;
	f->ip = ip;
	f->off = 0;
	f->readable = 0;
	f->writable = 1;
	f->flags = O_WRONLY;
	return fd;
}

int sys_chmod(struct trapframe *tf)
{
	char path[NNAME];
	int mode;

	if (argstr(tf, 0, path, NNAME) < 0 || argint(tf, 1, &mode) < 0)
		return -1;
	return fs_chmod(path, (uint)mode);
}

int sys_chown(struct trapframe *tf)
{
	char path[NNAME];
	int uid, gid;

	if (argstr(tf, 0, path, NNAME) < 0 ||
	    argint(tf, 1, &uid) < 0 || argint(tf, 2, &gid) < 0)
		return -1;
	return fs_chown(path, (uint)uid, (uint)gid);
}

/* utime(path, times)：times==NULL 则用当前时间 */
int sys_utime(struct trapframe *tf)
{
	char path[NNAME];
	uint uaddr;
	struct utimbuf ub;
	struct proc *p = myproc();
	uint at, mt;

	if (argstr(tf, 0, path, NNAME) < 0 || argaddr(tf, 1, &uaddr) < 0)
		return -1;
	if (!p || !proc_pagetable(p))
		return -1;
	if (uaddr == 0) {
		at = mt = timer_ticks() / TIMER_HZ;
	} else {
		if (copyin(proc_pagetable(p), &ub, uaddr, sizeof(ub)) < 0)
			return -1;
		at = ub.actime;
		mt = ub.modtime;
	}
	return fs_utime(path, at, mt);
}

/* fsync / sync：ramfs 无持久化，成功空操作；为 POSIX 完备性保留 */
int sys_fsync(struct trapframe *tf)
{
	int fd;
	struct file *f;

	if (argint(tf, 0, &fd) < 0)
		return -1;
	f = fdget(fd);
	if (!f || !f->ip)
		return -1;
	return 0;
}

int sys_sync(struct trapframe *tf)
{
	(void)tf;
	return 0;
}
