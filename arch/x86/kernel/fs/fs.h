/*
 * 文件系统对外头文件（umbrella）。
 * VFS 类型与 API 见 vfs.h；根后端见 ramfs/，块设备 FS 见 ext2/，procfs 见 proc/。
 */
#ifndef __FS_H__
#define __FS_H__

#include "vfs.h"

void fs_init(void);		/* fs/ramfs/inode.c */
void proc_init(void);		/* fs/proc/root.c */

#endif
