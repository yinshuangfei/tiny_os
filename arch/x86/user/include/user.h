/*
 * 用户态 C 程序最小头文件（与 usys.S 桩一致）。
 */
#ifndef __USER_USER_H__
#define __USER_USER_H__

#include "syscall.h"
#include "signal.h"
#include "dirent.h"
#include "sys/wait.h"
#include "termios.h"

struct timespec;

int execve(const char *filename, char *const argv[], char *const envp[]);
int exit(int) __attribute__((noreturn));
int fork(void);
int getpid(void);
/*
 * getcpu(2)：对齐 Linux i386 __NR_getcpu；写入 *cpu / *node，成功返回 0。
 * node 可为 NULL；无 NUMA 时写 0。
 */
int getcpu(unsigned int *cpu, unsigned int *node);
/* sched_getcpu(3)：当前逻辑 CPU 号（封装 getcpu） */
int sched_getcpu(void);
/* get_nprocs(3)：在线 CPU 数（读 /proc/cpuinfo；Linux 无 SYS_ncpu） */
int get_nprocs(void);
int kill(int pid, int sig);
sighandler_t signal(int sig, sighandler_t handler);
int sigreturn(void);
int nanosleep(const struct timespec *req, struct timespec *rem);
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);
int waitpid(int pid, int *status, int options);
int wait(int *status);
void *brk(void *addr);
void *sbrk(int incr);
void *mmap(void *addr, unsigned int length, int prot, int flags,
	   int fd, int offset);
int munmap(void *addr, unsigned int length);
int read(int fd, void *buf, int n);
int write(int, const void *, int);
int open(const char *path, int flags);
int close(int fd);
int dup(int fd);
int pipe(int fd[2]);
int ioctl(int fd, unsigned int request, void *arg);
int lseek(int fd, int offset, int whence);
int fstat(int fd, struct stat *st);
int stat(const char *path, struct stat *st);
int readlink(const char *path, char *buf, int bufsiz);
int chdir(const char *path);
int getcwd(char *buf, int size);
int mkdir(const char *path, int mode);
int rmdir(const char *path);
int link(const char *oldpath, const char *newpath);
int symlink(const char *target, const char *linkpath);
int unlink(const char *path);
int rename(const char *oldpath, const char *newpath);

int printf(const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, unsigned int size, const char *fmt, ...);

/* string.h */
unsigned int strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, unsigned int n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, unsigned int n);

#endif
