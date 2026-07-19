/*
 * 用户态 C 程序最小头文件（与 usys.S 桩一致）。
 */
#ifndef __USER_USER_H__
#define __USER_USER_H__

#include "syscall.h"
#include "signal.h"
#include "dirent.h"

struct timespec;

int execve(const char *filename, char *const argv[], char *const envp[]);
int exit(int) __attribute__((noreturn));
int fork(void);
int getpid(void);
int kill(int pid, int sig);
sighandler_t signal(int sig, sighandler_t handler);
int sigreturn(void);
int nanosleep(const struct timespec *req, struct timespec *rem);
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);
int waitpid(int pid, int *status, int options);
int read(int fd, void *buf, int n);
int write(int, const void *, int);
int open(const char *path, int flags);
int close(int fd);
int dup(int fd);
int pipe(int fd[2]);
int lseek(int fd, int offset, int whence);
int fstat(int fd, struct stat *st);
int stat(const char *path, struct stat *st);
int chdir(const char *path);
int getcwd(char *buf, int size);
int mkdir(const char *path, int mode);
int rmdir(const char *path);
int link(const char *oldpath, const char *newpath);
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
