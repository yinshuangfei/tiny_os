#ifndef __EXECVE_H__
#define __EXECVE_H__

#include "trap.h"

struct proc;

void user_enter_ring3(struct proc *p) __attribute__((noreturn));

int exec_load(struct proc *p, struct trapframe *tf, const void *blob, uint size,
	      const char *name);
int execve(struct proc *p, struct trapframe *tf, const char *path);
void kernel_execve(struct proc *p, const char *path) __attribute__((noreturn));

#endif
