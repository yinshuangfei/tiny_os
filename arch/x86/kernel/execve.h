#ifndef __EXECVE_H__
#define __EXECVE_H__

#include "interrupt.h"

struct proc;

int exec_load(struct proc *p, struct trapframe *tf, const void *blob, uint size,
	      const char *name);
int execve(struct proc *p, struct trapframe *tf, const char *path);

#endif
