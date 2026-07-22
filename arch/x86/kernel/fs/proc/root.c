/*
 * /proc 根目录与初始化（对齐 Linux fs/proc/root.c 教学子集）。
 */
#include "../../types.h"
#include "../../defs.h"
#include "../../param.h"
#include "../../printk.h"
#include "../../proc.h"
#include "../../lock/proc_lock.h"
#include "../fs.h"
#include "internal.h"

static const struct {
	const char	*name;
	proc_show_t	show;
} proc_root_files[] = {
	{ "meminfo",	meminfo_show },
	{ "cpuinfo",	cpuinfo_show },
	{ "devices",	devices_show },
};

#define PROC_ROOT_NFILES \
	(sizeof(proc_root_files) / sizeof(proc_root_files[0]))

static struct dentry *proc_dirlookup(struct inode *dp, const char *name)
{
	struct dentry *d;

	if (!dp || dp->type != T_DIR)
		return 0;
	for (d = dp->dents; d; d = d->next) {
		if (strcmp(d->name, name) == 0)
			return d;
	}
	return 0;
}

static struct inode *proc_root_lookup(struct inode *dir, const char *name)
{
	struct dentry *de;
	int pid;

	(void)dir;
	/* 静态项含 meminfo / self 等 */
	de = proc_dirlookup(proc_dir, name);
	if (de)
		return fs_idup(de->ip);

	pid = parse_pid_name(name);
	if (pid < 0 || !proc_alive(pid))
		return 0;

	return procfs_get(pid, PF_PID_DIR, proc_dir);
}

/*
 * 目录流：[静态 dents（含 self）...][存活 pid...]
 */
static int proc_root_read(struct inode *ip, char *dst, uint off, uint n)
{
	struct dentry *d;
	uint esz = sizeof(struct dirent);
	uint idx, logical, total;
	char namebuf[16];
	int pi;

	if (!ip || ip->type != T_DIR || off % esz)
		return -1;

	idx = off / esz;
	total = 0;
	logical = 0;

	for (d = ip->dents; d; d = d->next) {
		if (logical >= idx) {
			if (!proc_emit_dirent(dst, &n, off, &total,
					      d->ip ? d->ip->inum : 0,
					      d->ip ? proc_inode_to_dtype(d->ip->type)
						     : DT_UNKNOWN,
					      d->name))
				return (int)total;
			dst += esz;
		}
		logical++;
	}

	acquire(&proc_lock);
	for (pi = 0; pi < NPROC; pi++) {
		struct proc *p = &proc_table[pi];

		if (p->state == UNUSED || p->pid <= 0)
			continue;
		if (logical >= idx) {
			snprintf(namebuf, sizeof(namebuf), "%d", p->pid);
			if (!proc_emit_dirent(dst, &n, off, &total,
					      PROC_INO_BASE | (uint)p->pid,
					      DT_DIR, namebuf))
				break;
			dst += esz;
		}
		logical++;
	}
	release(&proc_lock);
	return (int)total;
}

static void proc_root_evict(struct inode *ip)
{
	struct dentry *d, *nx;

	for (d = ip->dents; d; d = nx) {
		nx = d->next;
		fs_iput(d->ip);
		kfree(d);
	}
	memset(ip, 0, sizeof(*ip));
}

const struct inode_operations proc_root_iops = {
	.lookup	= proc_root_lookup,
	.evict	= proc_root_evict,
	.read	= proc_root_read,
	.write	= proc_ro_write,
};

void proc_init(void)
{
	struct inode *ip;
	uint i;

	procfs_inode_init();

	if (fs_mkdir("/proc") < 0)
		panic("proc_init: /proc");

	for (i = 0; i < PROC_ROOT_NFILES; i++) {
		if (proc_create(proc_root_files[i].name,
				proc_root_files[i].show) < 0)
			panic("proc_init: create");
	}

	/*
	 * /proc/self：经 symlink 创建（占位目标），再换成动态 get_link 风格 i_op，
	 * 使 readlink 返回当前 pid。须在覆盖 /proc 的 i_op 之前完成。
	 */
	if (fs_symlink("0", "/proc/self") < 0)
		panic("proc_init: symlink self");
	ip = fs_namei_nofollow("/proc/self");
	if (!ip)
		panic("proc_init: self");
	if (ip->data) {
		kfree(ip->data);
		ip->data = 0;
	}
	ip->size = 0;
	ip->i_op = &proc_self_iops;
	fs_iput(ip);

	proc_dir = fs_namei("/proc");
	if (!proc_dir)
		panic("proc_init: namei /proc");
	proc_dir->i_op = &proc_root_iops;

	printk(KERN_INFO "fs: /proc ready (meminfo,cpuinfo,devices,self,<pid>)\n");
}
