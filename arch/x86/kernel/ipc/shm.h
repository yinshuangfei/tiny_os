/*
 * SysV 共享内存（教学子集）。
 * 系统调用号对齐 Linux i386：shmget=29, shmat=30, shmctl=31, shmdt=67。
 */
#ifndef __IPC_SHM_H__
#define __IPC_SHM_H__

#include "../types.h"
#include "../trap.h"

struct proc;
struct vma;

/* 创建或查找段；成功返回 shmid，失败 -1 */
int do_shmget(int key, uint size, int shmflg);

/* 附着；成功返回用户 VA，失败 (uint)-1 */
uint do_shmat(int shmid, uint shmaddr, int shmflg);

/* 分离；成功 0，失败 -1 */
int do_shmdt(uint shmaddr);

/* 控制；教学版仅支持 IPC_RMID */
int do_shmctl(int shmid, int cmd, uint buf);

/* fork 后：撤销 COW，恢复父子共享可写映射，并增加 nattch */
void shm_fork_fix(struct proc *child, struct proc *parent);

/* exit / exec：卸掉本进程全部 shm 附着 */
void shm_detach_all(struct proc *p);

/* munmap 整段 shm VMA 时走分离路径 */
int shm_munmap_vma(struct proc *p, struct vma *v);

/* 缺页：shm VMA 从段物理页映射（非填零） */
int shm_demand_fault(struct proc *p, struct vma *v, uint page, int write);

int sys_shmget(struct trapframe *tf);
int sys_shmat(struct trapframe *tf);
int sys_shmdt(struct trapframe *tf);
int sys_shmctl(struct trapframe *tf);

#endif
