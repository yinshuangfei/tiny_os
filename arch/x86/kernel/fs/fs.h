/*
 * 文件系统对外头文件（umbrella）。
 * VFS 类型与 API 见 vfs.h（含 inode / file）；具体后端（如 ramfs）提供 fs_init。
 */
#ifndef __FS_H__
#define __FS_H__

#include "vfs.h"

void fs_init(void);
void proc_init(void);

#endif
