#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#include "../user/syscall.h"
#include "trap.h"

void syscall(struct trapframe *tf);

int argint(struct trapframe *tf, int n, int *ip);
int argaddr(struct trapframe *tf, int n, uint *ip);
int argstr(struct trapframe *tf, int n, char *buf, int max);

int sys_exit(struct trapframe *tf);
int sys_execve(struct trapframe *tf);
int sys_getpid(struct trapframe *tf);
int sys_write(struct trapframe *tf);

#endif
