#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#include "../user/include/syscall.h"
#include "trap.h"

void syscall(struct trapframe *tf);

int argint(struct trapframe *tf, int n, int *ip);
int argaddr(struct trapframe *tf, int n, uint *ip);
int argstr(struct trapframe *tf, int n, char *buf, int max);

int sys_exit(struct trapframe *tf);
int sys_execve(struct trapframe *tf);
int sys_fork(struct trapframe *tf);
int sys_getpid(struct trapframe *tf);
int sys_read(struct trapframe *tf);
int sys_nanosleep(struct trapframe *tf);
int sys_waitpid(struct trapframe *tf);
int sys_write(struct trapframe *tf);
int sys_open(struct trapframe *tf);
int sys_close(struct trapframe *tf);
int sys_fstat(struct trapframe *tf);
int sys_stat(struct trapframe *tf);
int sys_chdir(struct trapframe *tf);
int sys_getcwd(struct trapframe *tf);
int sys_mkdir(struct trapframe *tf);
int sys_rmdir(struct trapframe *tf);
int sys_kill(struct trapframe *tf);
int sys_signal(struct trapframe *tf);
int sys_sigreturn(struct trapframe *tf);

#endif
