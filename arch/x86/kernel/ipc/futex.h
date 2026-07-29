/*
 * futex（教学子集，对齐 Linux i386 __NR_futex）。
 * 仅实现 FUTEX_WAIT / FUTEX_WAKE；等待键为用户字所在物理地址，
 * 以便跨进程共享同一物理页（如 SysV shm）时能互相唤醒。
 */
#ifndef __IPC_FUTEX_H__
#define __IPC_FUTEX_H__

#include "../trap.h"

#define FUTEX_WAIT		0
#define FUTEX_WAKE		1
#define FUTEX_PRIVATE_FLAG	128
#define FUTEX_CMD_MASK		0x7f

int sys_futex(struct trapframe *tf);

#endif
