/*
 * 用户态 C 程序最小头文件（与 usys.S 桩一致）。
 */
#ifndef __USER_USER_H__
#define __USER_USER_H__

#include "syscall.h"

int execve(const char *, char *const *, char *const *);
int exit(int) __attribute__((noreturn));
int getpid(void);
int write(int, const void *, int);

#endif
