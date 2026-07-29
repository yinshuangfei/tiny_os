/*
 * POSIX 信号量（对齐 Linux <semaphore.h> 教学子集）。
 *
 * 与 Linux/glibc 相同：无专用系统调用；
 *   无名 — sem_t 在用户内存，wait/post 用原子操作 + futex；
 *   命名 — SysV shm 承载 sem_t（Linux 用 /dev/shm 文件 + mmap）。
 */
#ifndef __USER_SEMAPHORE_H__
#define __USER_SEMAPHORE_H__

#include "syscall.h"

#define SEM_VALUE_MAX	32767

typedef struct {
	volatile int value;
	int private;		/* 非 0：FUTEX_PRIVATE（仅本进程） */
} sem_t;

#define SEM_FAILED	((sem_t *)-1)

int sem_init(sem_t *sem, int pshared, unsigned int value);
int sem_destroy(sem_t *sem);

/* oflag 不含 O_CREAT 时 mode/value 忽略，传 0 即可 */
sem_t *sem_open(const char *name, int oflag, int mode, unsigned int value);
int sem_close(sem_t *sem);
int sem_unlink(const char *name);

int sem_wait(sem_t *sem);
int sem_trywait(sem_t *sem);
int sem_post(sem_t *sem);
int sem_getvalue(sem_t *sem, int *sval);

#endif
