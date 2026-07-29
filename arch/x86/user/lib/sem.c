/*
 * POSIX 信号量用户库（Linux 风格：原子 + futex；命名走 SysV shm）。
 */
#include "user.h"
#include "semaphore.h"

#define SEM_MAGIC	0x53454d31	/* 'SEM1' */
#define SEM_NAME_MAX	32

/* 命名对象：shm 段首布局 */
struct named_sem {
	int magic;
	sem_t sem;
};

/* 系统调用 futex
 * 参数：
 * uaddr：用户地址
 * op：操作类型
 * val：值
 * timeout：超时时间
 * uaddr2：用户地址2
 * val3：值3
 */
int futex(volatile int *uaddr, int op, int val, void *timeout,
	  volatile int *uaddr2, int val3);

static int futex_wait(volatile int *addr, int expected, int private)
{
	int op = FUTEX_WAIT;

	if (private)
		op |= FUTEX_PRIVATE_FLAG;
	return futex(addr, op, expected, 0, 0, 0);
}

static int futex_wake(volatile int *addr, int n, int private)
{
	int op = FUTEX_WAKE;

	if (private)
		op |= FUTEX_PRIVATE_FLAG;
	return futex(addr, op, n, 0, 0, 0);
}

/* 成功减一返回 0；值为 0 返回 -1 */
static int sem_trydec(sem_t *sem)
{
	int v;

	for (;;) {
		v = sem->value;
		if (v <= 0)
			return -1;
		if (__sync_bool_compare_and_swap(&sem->value, v, v - 1))
			return 0;
	}
}

static int name_copy(char *dst, const char *src, int max)
{
	int i;

	if (!src || !src[0] || max < 2)
		return -1;
	if (src[0] == '/')
		src++;
	if (!src[0])
		return -1;
	for (i = 0; i < max - 1 && src[i]; i++)
		dst[i] = src[i];
	if (src[i])
		return -1;
	dst[i] = '\0';
	return 0;
}

/* 名字 → SysV key（非 0，避免 IPC_PRIVATE） */
static int name_to_key(const char *name)
{
	unsigned int h = 5381;
	int i;

	for (i = 0; name[i]; i++)
		h = ((h << 5) + h) + (unsigned char)name[i];
	if (h == 0)
		h = 1;
	return (int)h;
}

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
	if (!sem || value > SEM_VALUE_MAX)
		return -1;
	sem->value = (int)value;
	sem->private = pshared ? 0 : 1;
	return 0;
}

int sem_destroy(sem_t *sem)
{
	if (!sem)
		return -1;
	sem->value = 0;
	return 0;
}

int sem_trywait(sem_t *sem)
{
	if (!sem)
		return -1;
	return sem_trydec(sem);
}

int sem_wait(sem_t *sem)
{
	if (!sem)
		return -1;
	for (;;) {
		if (sem_trydec(sem) == 0)
			return 0;
		/* 值为 0 时睡眠；被唤醒后重试 */
		(void)futex_wait(&sem->value, 0, sem->private);
	}
}

int sem_post(sem_t *sem)
{
	int v;

	if (!sem)
		return -1;
	v = __sync_fetch_and_add(&sem->value, 1);
	if (v >= SEM_VALUE_MAX) {
		__sync_fetch_and_sub(&sem->value, 1);
		return -1;
	}
	/* 原值 ≤0 时可能有等待者（简化：总是 wake 1，多余 wake 无害） */
	if (v == 0)
		(void)futex_wake(&sem->value, 1, sem->private);
	return 0;
}

int sem_getvalue(sem_t *sem, int *sval)
{
	if (!sem || !sval)
		return -1;
	*sval = sem->value;
	if (*sval < 0)
		*sval = 0;
	return 0;
}

sem_t *sem_open(const char *name, int oflag, int mode, unsigned int value)
{
	char nbuf[SEM_NAME_MAX];
	struct named_sem *obj;
	int key, flg, shmid, created = 0;
	void *addr;

	(void)mode;
	if (name_copy(nbuf, name, SEM_NAME_MAX) < 0)
		return SEM_FAILED;
	if ((oflag & O_CREAT) && value > SEM_VALUE_MAX)
		return SEM_FAILED;

	key = name_to_key(nbuf);
	flg = 0;
	if (oflag & O_CREAT)
		flg |= IPC_CREAT;
	if ((oflag & O_CREAT) && (oflag & O_EXCL))
		flg |= IPC_EXCL;

	shmid = shmget(key, sizeof(*obj), flg);
	if (shmid < 0)
		return SEM_FAILED;

	if ((oflag & O_CREAT) && (oflag & O_EXCL))
		created = 1;

	addr = shmat(shmid, 0, 0);
	if (addr == (void *)-1 || addr == 0)
		return SEM_FAILED;

	obj = (struct named_sem *)addr;
	if (created || obj->magic != SEM_MAGIC) {
		obj->sem.value = (oflag & O_CREAT) ? (int)value : 0;
		obj->sem.private = 0;	/* 跨进程共享 */
		__sync_synchronize();
		obj->magic = SEM_MAGIC;
	}
	return &obj->sem;
}

int sem_close(sem_t *sem)
{
	struct named_sem *obj;

	if (!sem)
		return -1;
	obj = (struct named_sem *)((char *)sem - (unsigned int)&((struct named_sem *)0)->sem);
	if (obj->magic != SEM_MAGIC)
		return -1;
	return shmdt(obj);
}

int sem_unlink(const char *name)
{
	char nbuf[SEM_NAME_MAX];
	int key, shmid;

	if (name_copy(nbuf, name, SEM_NAME_MAX) < 0)
		return -1;
	key = name_to_key(nbuf);
	shmid = shmget(key, sizeof(struct named_sem), 0);
	if (shmid < 0)
		return -1;
	return shmctl(shmid, IPC_RMID, 0);
}
