/*
 * 用户态 C 程序最小头文件（与 usys.S 桩一致）。
 */
#ifndef __USER_USER_H__
#define __USER_USER_H__

#include "syscall.h"

struct timespec;

int execve(const char *, char *const *, char *const *);
int exit(int) __attribute__((noreturn));
int fork(void);
int getpid(void);
int nanosleep(const struct timespec *req, struct timespec *rem);
int waitpid(int pid, int *status, int options);
int write(int, const void *, int);

int printf(const char *fmt, ...);

#endif
