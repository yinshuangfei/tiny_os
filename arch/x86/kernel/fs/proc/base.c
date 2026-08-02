/*
 * /proc/<pid>/（对齐 Linux fs/proc/base.c 教学子集）。
 */
#include "../../types.h"
#include "../../defs.h"
#include "../../param.h"
#include "../../proc.h"
#include "../../mm/memlayout.h"
#include "../../major.h"
#include "../../lock/proc_lock.h"
#include "../fs.h"
#include "internal.h"

static char state_char(int st)
{
	switch (st) {
	case RUNNING:
	case RUNNABLE:
		return 'R';
	case SLEEPING:
	case USED:
		return 'S';
	case ZOMBIE:
		return 'Z';
	default:
		return '?';
	}
}

static const char *state_desc(int st)
{
	switch (st) {
	case RUNNING:
		return "running";
	case RUNNABLE:
		return "runnable";
	case SLEEPING:
		return "sleeping";
	case USED:
		return "used";
	case ZOMBIE:
		return "zombie";
	default:
		return "unknown";
	}
}

static int proc_pid_status(struct proc_snap *s, char *buf, uint size)
{
	return snprintf(buf, size,
		"Name:\t%s\n"
		"State:\t%c (%s)\n"
		"Tgid:\t%d\n"
		"Pid:\t%d\n"
		"PPid:\t%d\n"
		"VmSize:\t%8u kB\n"
		"VmRSS:\t%8u kB\n",
		s->name,
		state_char(s->state), state_desc(s->state),
		s->pid, s->pid, s->ppid,
		s->vmsize_kb, s->vmrss_kb);
}

static int proc_pid_cmdline(struct proc_snap *s, char *buf, uint size)
{
	return snprintf(buf, size, "%s\n", s->name);
}

static int proc_pid_stat(struct proc_snap *s, char *buf, uint size)
{
	return snprintf(buf, size,
			"%d (%s) %c %d 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 %u %u\n",
			s->pid, s->name, state_char(s->state), s->ppid,
			s->vmsize_kb, s->vmrss_kb);
}

/* maps：权限串 rwxp（教学：匿名区一律 p） */
static void maps_perm(int prot, char out[5])
{
	out[0] = (prot & PROT_READ) ? 'r' : '-';
	out[1] = (prot & PROT_WRITE) ? 'w' : '-';
	out[2] = (prot & PROT_EXEC) ? 'x' : '-';
	out[3] = 'p';
	out[4] = '\0';
}

static int maps_line(char *buf, uint size, uint *len,
		     uint start, uint end, int prot, const char *tag)
{
	char perm[5];
	int w;

	if (*len >= size - 1)
		return -1;
	maps_perm(prot, perm);
	w = snprintf(buf + *len, size - *len,
		     "%08x-%08x %s 00000000 00:00 0\t\t%s\n",
		     start, end, perm, tag);
	if (w < 0)
		return -1;
	*len += (uint)w;
	return 0;
}

static int proc_pid_maps(struct proc_snap *s, char *buf, uint size)
{
	uint len = 0;
	int i;
	int heap_prot = PROT_READ | PROT_WRITE;

	if (!s->has_user_mm && s->brk <= USERBASE)
		return 0;

	/* [USERBASE, brk_start)：代码/数据 */
	if (s->brk_start > USERBASE) {
		if (maps_line(buf, size, &len, USERBASE, s->brk_start,
			      PROT_READ | PROT_EXEC, "[text]") < 0)
			return (int)len;
	}
	/* [brk_start, brk)：堆 */
	if (s->brk > s->brk_start) {
		if (maps_line(buf, size, &len, s->brk_start, s->brk,
			      heap_prot, "[heap]") < 0)
			return (int)len;
	} else if (s->brk > USERBASE && s->brk_start <= USERBASE) {
		if (maps_line(buf, size, &len, USERBASE, s->brk,
			      heap_prot, "[heap]") < 0)
			return (int)len;
	}

	for (i = 0; i < NVMA; i++) {
		if (!s->vmas[i].used)
			continue;
		if (maps_line(buf, size, &len, s->vmas[i].start,
			      s->vmas[i].end, s->vmas[i].prot, "") < 0)
			return (int)len;
	}

	/* 用户栈：[USERSTACK_BOTTOM, USERSTACK) */
	if (maps_line(buf, size, &len, USERSTACK_BOTTOM, USERSTACK,
		      PROT_READ | PROT_WRITE, "[stack]") < 0)
		return (int)len;

	return (int)len;
}

/* 对齐 Linux tgid_base_stuff[]（普通文件） */
static const struct pid_entry tgid_base_stuff[] = {
	{ "status",	PF_STATUS,	proc_pid_status },
	{ "cmdline",	PF_CMDLINE,	proc_pid_cmdline },
	{ "stat",	PF_STAT,	proc_pid_stat },
	{ "maps",	PF_MAPS,	proc_pid_maps },
};

#define TGID_BASE_LEN	(sizeof(tgid_base_stuff) / sizeof(tgid_base_stuff[0]))

static const struct pid_entry *pid_entry_lookup(const char *name)
{
	uint i;

	for (i = 0; i < TGID_BASE_LEN; i++) {
		if (strcmp(name, tgid_base_stuff[i].name) == 0)
			return &tgid_base_stuff[i];
	}
	return 0;
}

static const struct pid_entry *pid_entry_by_kind(int kind)
{
	uint i;

	for (i = 0; i < TGID_BASE_LEN; i++) {
		if (tgid_base_stuff[i].kind == kind)
			return &tgid_base_stuff[i];
	}
	return 0;
}

static int proc_pid_file_read(struct inode *ip, char *dst, uint off, uint n)
{
	struct procfs_node *pn;
	const struct pid_entry *pe;
	struct proc_snap snap;
	char mapsbuf[PROCFS_MAPS_BUF];
	char buf[PROCFS_BUF];
	char *use;
	uint use_sz;
	int w;

	pn = pn_of(ip);
	if (!pn || pn->kind == PF_FREE || pn->kind == PF_PID_DIR ||
	    pn->kind == PF_FD_DIR || pn->kind == PF_FD)
		return -1;
	pe = pid_entry_by_kind(pn->kind);
	if (!pe || !pe->show)
		return -1;
	if (snap_proc(pn->pid, &snap) < 0)
		return -1;

	if (pn->kind == PF_MAPS) {
		use = mapsbuf;
		use_sz = sizeof(mapsbuf);
	} else {
		use = buf;
		use_sz = sizeof(buf);
	}
	w = pe->show(&snap, use, use_sz);
	if (w < 0)
		return -1;
	return proc_copy_buf(ip, use, (uint)w, dst, off, n);
}

const struct inode_operations proc_pid_file_iops = {
	.evict	= procfs_node_evict,
	.read	= proc_pid_file_read,
	.write	= proc_ro_write,
};

/* ---------- /proc/<pid>/fd/ ---------- */

static int parse_fd_name(const char *name)
{
	int fd = 0;

	if (!name || !*name)
		return -1;
	for (; *name; name++) {
		if (*name < '0' || *name > '9')
			return -1;
		fd = fd * 10 + (*name - '0');
		if (fd < 0 || fd >= NOFILE)
			return -1;
	}
	return fd;
}

static struct inode *proc_fd_dir_lookup(struct inode *dir, const char *name)
{
	struct procfs_node *pn;
	int fd;

	pn = pn_of(dir);
	if (!pn || pn->kind != PF_FD_DIR)
		return 0;
	if (!proc_alive(pn->pid))
		return 0;
	fd = parse_fd_name(name);
	if (fd < 0)
		return 0;

	acquire(&proc_lock);
	{
		int i;
		struct proc *p = 0;

		for (i = 0; i < NPROC; i++) {
			if (proc_table[i].pid == pn->pid &&
			    proc_table[i].state != UNUSED) {
				p = &proc_table[i];
				break;
			}
		}
		if (!p || !p->ofile[fd]) {
			release(&proc_lock);
			return 0;
		}
	}
	release(&proc_lock);
	return procfs_get(pn->pid, PF_FD, fd, dir);
}

static int proc_fd_dir_read(struct inode *ip, char *dst, uint off, uint n)
{
	struct procfs_node *pn;
	struct proc *p = 0;
	uint esz = sizeof(struct dirent);
	uint idx, logical, total;
	char namebuf[16];
	int i, fd;
	char openfds[NOFILE];

	pn = pn_of(ip);
	if (!pn || pn->kind != PF_FD_DIR || off % esz)
		return -1;

	memset(openfds, 0, sizeof(openfds));
	acquire(&proc_lock);
	for (i = 0; i < NPROC; i++) {
		if (proc_table[i].pid == pn->pid &&
		    proc_table[i].state != UNUSED) {
			p = &proc_table[i];
			break;
		}
	}
	if (p) {
		for (fd = 0; fd < NOFILE; fd++) {
			if (p->ofile[fd])
				openfds[fd] = 1;
		}
	}
	release(&proc_lock);
	if (!p)
		return -1;

	idx = off / esz;
	total = 0;
	logical = 0;
	for (fd = 0; fd < NOFILE; fd++) {
		if (!openfds[fd])
			continue;
		if (logical >= idx) {
			snprintf(namebuf, sizeof(namebuf), "%d", fd);
			if (!proc_emit_dirent(dst, &n, off, &total,
					      ip->inum + 1 + (uint)fd,
					      DT_LNK, namebuf))
				break;
			dst += esz;
		}
		logical++;
	}
	return (int)total;
}

const struct inode_operations proc_fd_dir_iops = {
	.lookup	= proc_fd_dir_lookup,
	.evict	= procfs_node_evict,
	.read	= proc_fd_dir_read,
	.write	= proc_ro_write,
};

/*
 * /proc/<pid>/fd/N：符号链接风格目标串（对齐 Linux readlink 教学子集）。
 */
static int proc_fd_link_read(struct inode *ip, char *dst, uint off, uint n)
{
	struct procfs_node *pn;
	struct file *f;
	char buf[64];
	int w = -1;
	int i;

	pn = pn_of(ip);
	if (!pn || pn->kind != PF_FD)
		return -1;

	acquire(&proc_lock);
	for (i = 0; i < NPROC; i++) {
		struct proc *p = &proc_table[i];

		if (p->pid != pn->pid || p->state == UNUSED)
			continue;
		f = p->ofile[pn->aux];
		if (!f) {
			release(&proc_lock);
			return -1;
		}
		switch (f->type) {
		case FD_CHAR:
			if (f->ip)
				w = snprintf(buf, sizeof(buf),
					     "char:%u:%u",
					     MAJOR(f->ip->rdev),
					     MINOR(f->ip->rdev));
			else
				w = snprintf(buf, sizeof(buf), "char:");
			break;
		case FD_BLOCK:
			if (f->ip)
				w = snprintf(buf, sizeof(buf),
					     "block:%u:%u",
					     MAJOR(f->ip->rdev),
					     MINOR(f->ip->rdev));
			else
				w = snprintf(buf, sizeof(buf), "block:");
			break;
		case FD_INODE:
			if (f->ip && f->ip->type == T_FIFO)
				w = snprintf(buf, sizeof(buf),
					     "pipe:[%u]", f->ip->inum);
			else if (f->ip && f->ip->type == T_DIR)
				w = snprintf(buf, sizeof(buf),
					     "dir:[%u]", f->ip->inum);
			else if (f->ip)
				w = snprintf(buf, sizeof(buf),
					     "file:[%u]", f->ip->inum);
			else
				w = snprintf(buf, sizeof(buf), "file:");
			break;
		default:
			w = snprintf(buf, sizeof(buf), "anon");
			break;
		}
		break;
	}
	release(&proc_lock);
	if (w < 0)
		return -1;
	return proc_copy_buf(ip, buf, (uint)w, dst, off, n);
}

const struct inode_operations proc_fd_link_iops = {
	.evict	= procfs_node_evict,
	.read	= proc_fd_link_read,
	.write	= proc_ro_write,
};

/* ---------- /proc/<pid>/ 目录 ---------- */

static struct inode *proc_pid_dir_lookup(struct inode *dir, const char *name)
{
	struct procfs_node *pn;
	const struct pid_entry *pe;

	pn = pn_of(dir);
	if (!pn || pn->kind != PF_PID_DIR)
		return 0;
	if (!proc_alive(pn->pid))
		return 0;

	if (strcmp(name, "fd") == 0)
		return procfs_get(pn->pid, PF_FD_DIR, 0, dir);

	pe = pid_entry_lookup(name);
	if (!pe)
		return 0;
	return procfs_get(pn->pid, pe->kind, 0, dir);
}

static int proc_pid_dir_read(struct inode *ip, char *dst, uint off, uint n)
{
	struct procfs_node *pn;
	uint esz = sizeof(struct dirent);
	uint idx, logical, total;
	uint i;

	pn = pn_of(ip);
	if (!pn || pn->kind != PF_PID_DIR || off % esz)
		return -1;

	idx = off / esz;
	total = 0;
	logical = 0;

	for (i = 0; i < TGID_BASE_LEN; i++) {
		if (logical >= idx) {
			if (!proc_emit_dirent(dst, &n, off, &total,
					      ip->inum + 1 + i, DT_REG,
					      tgid_base_stuff[i].name))
				return (int)total;
			dst += esz;
		}
		logical++;
	}
	if (logical >= idx) {
		if (!proc_emit_dirent(dst, &n, off, &total,
				      ip->inum + 100, DT_DIR, "fd"))
			return (int)total;
	}
	return (int)total;
}

const struct inode_operations proc_pid_dir_iops = {
	.lookup	= proc_pid_dir_lookup,
	.evict	= procfs_node_evict,
	.read	= proc_pid_dir_read,
	.write	= proc_ro_write,
};
