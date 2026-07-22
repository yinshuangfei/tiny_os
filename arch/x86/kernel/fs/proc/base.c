/*
 * /proc/<pid>/（对齐 Linux fs/proc/base.c 教学子集）。
 */
#include "../../types.h"
#include "../../defs.h"
#include "../../param.h"
#include "../../proc.h"
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
		s->sz / 1024, s->brk / 1024);
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
			s->sz, s->brk);
}

/* 对齐 Linux tgid_base_stuff[] */
static const struct pid_entry tgid_base_stuff[] = {
	{ "status",	PF_STATUS,	proc_pid_status },
	{ "cmdline",	PF_CMDLINE,	proc_pid_cmdline },
	{ "stat",	PF_STAT,	proc_pid_stat },
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
	char buf[PROCFS_BUF];
	int w;

	pn = pn_of(ip);
	if (!pn || pn->kind == PF_FREE || pn->kind == PF_PID_DIR)
		return -1;
	pe = pid_entry_by_kind(pn->kind);
	if (!pe || !pe->show)
		return -1;
	if (snap_proc(pn->pid, &snap) < 0)
		return -1;
	w = pe->show(&snap, buf, sizeof(buf));
	if (w < 0)
		return -1;
	return proc_copy_buf(ip, buf, (uint)w, dst, off, n);
}

const struct inode_operations proc_pid_file_iops = {
	.evict	= procfs_node_evict,
	.read	= proc_pid_file_read,
	.write	= proc_ro_write,
};

static struct inode *proc_pid_dir_lookup(struct inode *dir, const char *name)
{
	struct procfs_node *pn;
	const struct pid_entry *pe;

	pn = pn_of(dir);
	if (!pn || pn->kind != PF_PID_DIR)
		return 0;
	if (!proc_alive(pn->pid))
		return 0;
	pe = pid_entry_lookup(name);
	if (!pe)
		return 0;
	return procfs_get(pn->pid, pe->kind, dir);
}

static int proc_pid_dir_read(struct inode *ip, char *dst, uint off, uint n)
{
	struct procfs_node *pn;
	uint esz = sizeof(struct dirent);
	uint idx, i, total;

	pn = pn_of(ip);
	if (!pn || pn->kind != PF_PID_DIR || off % esz)
		return -1;

	idx = off / esz;
	total = 0;
	for (i = 0; i < TGID_BASE_LEN; i++) {
		if (i < idx)
			continue;
		if (!proc_emit_dirent(dst, &n, off, &total,
				      ip->inum + 1 + i, DT_REG,
				      tgid_base_stuff[i].name))
			break;
		dst += esz;
	}
	return (int)total;
}

const struct inode_operations proc_pid_dir_iops = {
	.lookup	= proc_pid_dir_lookup,
	.evict	= procfs_node_evict,
	.read	= proc_pid_dir_read,
	.write	= proc_ro_write,
};
