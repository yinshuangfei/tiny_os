/*
 * procfs 内部头文件（对齐 Linux fs/proc/internal.h 教学子集）。
 */
#ifndef __FS_PROC_INTERNAL_H__
#define __FS_PROC_INTERNAL_H__

#include "../../types.h"
#include "../../param.h"
#include "../../proc.h"
#include "../vfs.h"

#define PROCFS_BUF	512
#define PROCFS_MAPS_BUF	2048
#define PROCFS_NNODES	128
#define PROC_INO_BASE	0x80000000u

enum procfs_kind {
	PF_FREE = 0,			/* 空闲 */
	PF_PID_DIR,			/* pid 进程目录 */
	PF_STATUS,			/* pid 状态 */
	PF_CMDLINE,			/* pid 命令行 */
	PF_STAT,			/* pid 状态 */
	PF_MAPS,
	PF_FD_DIR,
	PF_FD,			/* aux = fd 号 */
};

struct procfs_node {
	struct inode	inode;		/* 文件系统节点 */
	int		pid;		/* 进程 ID */
	int		kind;		/* 节点类型 */
	int		aux;		/* PF_FD：文件描述符号 */
};

/* 进程快照（供 status/stat/maps） */
struct proc_snap {
	int pid;			/* 进程 ID */
	int ppid;			/* 父进程 ID */
	int state;			/* 状态 */
	char name[NNAME];		/* 进程名称 */
	uint sz;			/* 进程大小 */
	uint brk;			/* 堆大小 */
	uint brk_start;
	uint vmsize_kb;		/* 虚拟地址空间约数（类 VmSize） */
	uint vmrss_kb;		/* 常驻页约数（类 VmRSS） */
	struct vma vmas[NVMA];
	int has_user_mm;	/* 1：仍有用户地址空间可展示 */
};

typedef int (*proc_show_t)(char *buf, uint size);

struct pid_entry {
	const char	*name;
	int		kind;
	int		(*show)(struct proc_snap *s, char *buf, uint size);
};

extern struct inode *proc_dir;
extern const struct inode_operations proc_pid_dir_iops;
extern const struct inode_operations proc_pid_file_iops;
extern const struct inode_operations proc_fd_dir_iops;
extern const struct inode_operations proc_fd_link_iops;
extern const struct inode_operations proc_self_iops;
extern const struct inode_operations proc_single_iops;
extern const struct inode_operations proc_root_iops;

int proc_ro_write(struct inode *ip, char *src, uint off, uint n);
void proc_nop_evict(struct inode *ip);
int proc_copy_buf(struct inode *ip, const char *buf, uint len,
		  char *dst, uint off, uint n);
int proc_emit_dirent(char *dst, uint *n, uint off, uint *total,
		     uint ino, unsigned char dtype, const char *name);
unsigned char proc_inode_to_dtype(short type);
int parse_pid_name(const char *name);
int snap_proc(int pid, struct proc_snap *s);
int proc_alive(int pid);

struct procfs_node *pn_of(struct inode *ip);
void procfs_node_evict(struct inode *ip);
struct inode *procfs_get(int pid, int kind, int aux, struct inode *parent);
void procfs_inode_init(void);

int proc_create(const char *name, proc_show_t show);

int meminfo_show(char *buf, uint size);
int cpuinfo_show(char *buf, uint size);
int devices_show(char *buf, uint size);
int uptime_show(char *buf, uint size);
int partitions_show(char *buf, uint size);

#endif
