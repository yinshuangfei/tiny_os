/*
 * procfs inode 池与公用读路径（对齐 Linux fs/proc/inode.c 角色）。
 */
#include "../../types.h"
#include "../../defs.h"
#include "../../param.h"
#include "../../proc.h"
#include "../../mm/memlayout.h"
#include "../../mm/vm.h"
#include "../../lock/spinlock.h"
#include "../../lock/proc_lock.h"
#include "../fs.h"
#include "internal.h"

static struct procfs_node procfs_nodes[PROCFS_NNODES];
static struct spinlock procfs_lock;

struct inode *proc_dir;	/* /proc，由 root.c 在 init 时赋值 */

int proc_ro_write(struct inode *ip, char *src, uint off, uint n)
{
	(void)ip;
	(void)src;
	(void)off;
	(void)n;
	return -1;
}

void proc_nop_evict(struct inode *ip)
{
	(void)ip;
}

int proc_copy_buf(struct inode *ip, const char *buf, uint len,
		  char *dst, uint off, uint n)
{
	if (ip)
		ip->size = len;
	if (off >= len)
		return 0;
	if (off + n > len)
		n = len - off;
	memcpy(dst, buf + off, n);
	return (int)n;
}

/* 发出目录项 */
int proc_emit_dirent(char *dst, uint *n, uint off, uint *total,
		     uint ino, unsigned char dtype, const char *name)
{
	struct dirent de;
	uint esz = sizeof(struct dirent);
	uint j;

	if (*n < esz)
		return 0;
	memset(&de, 0, sizeof(de));
	de.d_ino = ino;
	de.d_reclen = (unsigned short)esz;
	de.d_off = off + *total + esz;
	de.d_type = dtype;
	strncpy(de.d_name, name, DIRSIZ);
	de.d_name[DIRSIZ - 1] = '\0';
	for (j = 0; j < esz; j++)
		dst[j] = ((char *)&de)[j];
	*n -= esz;
	*total += esz;
	return 1;
}

unsigned char proc_inode_to_dtype(short type)
{
	if (type == T_DIR)
		return DT_DIR;
	if (type == T_FILE)
		return DT_REG;
	if (type == T_LNK)
		return DT_LNK;
	return DT_UNKNOWN;
}

int parse_pid_name(const char *name)
{
	int pid = 0;

	if (!name || !*name)
		return -1;
	for (; *name; name++) {
		if (*name < '0' || *name > '9')
			return -1;
		pid = pid * 10 + (*name - '0');
		if (pid < 0)
			return -1;
	}
	return pid;
}

/* 虚拟地址空间约数：[USERBASE,brk) + 栈页 + 匿名 VMA */
static uint calc_vmsize_kb(struct proc *p)
{
	uint bytes = 0;
	int i;

	/* 内核线程无用户地址空间 */
	if (!p->pagetable && p->brk <= USERBASE)
		return 0;
	if (p->brk > USERBASE)
		bytes += p->brk - USERBASE;
	bytes += PGSIZE;
	for (i = 0; i < NVMA; i++) {
		if (p->vmas[i].used)
			bytes += p->vmas[i].end - p->vmas[i].start;
	}
	return bytes / 1024;
}

/* 常驻集：用户页表中 PTE_P 页数（类 get_mm_rss 教学近似） */
static uint calc_vmrss_kb(pagetable_t pgdir)
{
	uint va, pages = 0;

	if (!pgdir)
		return 0;
	for (va = USERBASE; va < USEREND; va += PGSIZE) {
		if (walkaddr(pgdir, va))
			pages++;
	}
	return pages * (PGSIZE / 1024);
}

/* 获取进程快照 */
int snap_proc(int pid, struct proc_snap *s)
{
	int i;

	acquire(&proc_lock);
	for (i = 0; i < NPROC; i++) {
		struct proc *p = &proc_table[i];

		if (p->pid != pid || p->state == UNUSED)
			continue;
		s->pid = p->pid;
		s->ppid = p->parent ? p->parent->pid : 0;
		s->state = p->state;
		s->sz = p->sz;
		s->brk = p->brk;
		s->brk_start = p->brk_start;
		s->vmsize_kb = calc_vmsize_kb(p);
		s->vmrss_kb = calc_vmrss_kb(p->pagetable);
		s->has_user_mm = p->pagetable != 0 || p->brk > USERBASE;
		memcpy(s->vmas, p->vmas, sizeof(s->vmas));
		strncpy(s->name, p->name, NNAME - 1);
		s->name[NNAME - 1] = '\0';
		release(&proc_lock);
		return 0;
	}
	release(&proc_lock);
	return -1;
}

int proc_alive(int pid)
{
	struct proc_snap s;

	return snap_proc(pid, &s) == 0;
}

/* 从 struct inode * 找回外包它的 struct procfs_node * */
struct procfs_node *pn_of(struct inode *ip)
{
	struct procfs_node *n;

	if (!ip)
		return 0;
	n = (struct procfs_node *)ip;
	if (n < procfs_nodes || n >= procfs_nodes + PROCFS_NNODES)
		return 0;
	return n;
}

void procfs_node_evict(struct inode *ip)
{
	struct procfs_node *n = pn_of(ip);

	if (!n)
		return;
	n->kind = PF_FREE;
	n->pid = 0;
	n->aux = 0;
	memset(ip, 0, sizeof(*ip));
}

static const char *kind_name(int kind)
{
	switch (kind) {
	case PF_STATUS:
		return "status";
	case PF_CMDLINE:
		return "cmdline";
	case PF_STAT:
		return "stat";
	case PF_MAPS:
		return "maps";
	case PF_FD_DIR:
		return "fd";
	default:
		return 0;
	}
}

/*
 * /proc/self：由 fs_symlink 创建后挂上本 i_op；
 * readlink 内容为当前进程 pid（对齐 Linux proc_self get_link）。
 */
static int proc_self_read(struct inode *ip, char *dst, uint off, uint n)
{
	struct proc *p = myproc();
	char buf[16];
	int w;

	if (!p || p->pid <= 0)
		return -1;
	w = snprintf(buf, sizeof(buf), "%d", p->pid);
	if (w < 0)
		return -1;
	return proc_copy_buf(ip, buf, (uint)w, dst, off, n);
}

static void proc_self_evict(struct inode *ip)
{
	if (ip->data) {
		kfree(ip->data);
		ip->data = 0;
	}
	ip->type = 0;
	ip->parent = 0;
	ip->i_op = 0;
	ip->size = 0;
	ip->inum = 0;
	ip->name[0] = '\0';
	ip->dents = 0;
	ip->rdev = 0;
}

const struct inode_operations proc_self_iops = {
	.evict	= proc_self_evict,
	.read	= proc_self_read,
	.write	= proc_ro_write,
};

/* 获取 procfs_node；aux 仅 PF_FD 使用 */
struct inode *procfs_get(int pid, int kind, int aux, struct inode *parent)
{
	int i;
	struct procfs_node *free_n = 0;
	struct inode *ip;
	const char *nm;

	acquire(&procfs_lock);
	for (i = 0; i < PROCFS_NNODES; i++) {
		struct procfs_node *n = &procfs_nodes[i];

		if (n->kind == kind && n->pid == pid && n->inode.type != 0 &&
		    (kind != PF_FD || n->aux == aux)) {
			ip = fs_idup(&n->inode);
			release(&procfs_lock);
			return ip;
		}
		if (!free_n && n->kind == PF_FREE)
			free_n = n;
	}
	if (!free_n) {
		release(&procfs_lock);
		return 0;
	}

	ip = &free_n->inode;
	memset(ip, 0, sizeof(*ip));
	free_n->pid = pid;
	free_n->kind = kind;
	free_n->aux = aux;
	ip->inum = PROC_INO_BASE | ((uint)(free_n - procfs_nodes) + 1);
	ip->ref = 1;
	ip->parent = parent ? parent : proc_dir;

	if (kind == PF_PID_DIR) {
		ip->type = T_DIR;
		ip->i_op = &proc_pid_dir_iops;
		snprintf(ip->name, DIRSIZ, "%d", pid);
	} else if (kind == PF_FD_DIR) {
		ip->type = T_DIR;
		ip->i_op = &proc_fd_dir_iops;
		strncpy(ip->name, "fd", DIRSIZ - 1);
	} else if (kind == PF_FD) {
		ip->type = T_LNK;
		ip->i_op = &proc_fd_link_iops;
		snprintf(ip->name, DIRSIZ, "%d", aux);
	} else {
		nm = kind_name(kind);
		if (!nm) {
			free_n->kind = PF_FREE;
			release(&procfs_lock);
			return 0;
		}
		ip->type = T_FILE;
		ip->i_op = &proc_pid_file_iops;
		strncpy(ip->name, nm, DIRSIZ - 1);
		ip->name[DIRSIZ - 1] = '\0';
	}
	release(&procfs_lock);
	return ip;
}

static int proc_single_read(struct inode *ip, char *dst, uint off, uint n)
{
	proc_show_t show;
	char buf[PROCFS_BUF];
	int w;

	if (!ip || !ip->data)
		return -1;
	show = (proc_show_t)(void *)ip->data;
	w = show(buf, sizeof(buf));
	if (w < 0)
		return -1;
	return proc_copy_buf(ip, buf, (uint)w, dst, off, n);
}

const struct inode_operations proc_single_iops = {
	.read	= proc_single_read,
	.write	= proc_ro_write,
	.evict	= proc_nop_evict,
};

/* 类 Linux proc_create() */
int proc_create(const char *name, proc_show_t show)
{
	char path[64];
	struct inode *ip;

	snprintf(path, sizeof(path), "/proc/%s", name);
	ip = fs_create(path, T_FILE);
	if (!ip)
		return -1;
	ip->i_op = &proc_single_iops;
	ip->data = (char *)(void *)show;
	ip->size = 0;
	fs_iput(ip);
	return 0;
}

void procfs_inode_init(void)
{
	initlock(&procfs_lock, "procfs");
	memset(procfs_nodes, 0, sizeof(procfs_nodes));
}
