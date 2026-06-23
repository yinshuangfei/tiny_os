#include "riscv.h"
#include "proc.h"
#include "defs.h"
#include "spinlock.h"
#include "memlayout.h"

struct cpu cpus[NCPU];

// kernel 进程
struct proc proc[NPROC];

int nextpid = 1;
struct spinlock pid_lock;

// initialize the proc table at boot time.
void procinit(void)
{
	struct proc *p;

	initlock(&pid_lock, "nextpid");
	for (p = proc; p < &proc[NPROC]; p++) {
		initlock(&p->lock, "proc");

		// Allocate a page for the process's kernel stack.
		// Map it high in memory, followed by an invalid
		// guard page.
		char *pa = kalloc();
		if(pa == 0)
			panic("kalloc");

		uint64 va = KSTACK((int) (p - proc));
		kvmmap(va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
		p->kstack = va;
	}
	kvminithart();
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid()
{
	int id = r_tp();
	return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu* mycpu(void) {
	int id = cpuid();
	struct cpu *c = &cpus[id];
	return c;
}

// Return the current struct proc *, or zero if none.
struct proc* myproc(void)
{
	// push_off();
	struct cpu *c = mycpu();
	struct proc *p = c->proc;
	// pop_off();
	return p;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
	struct proc *p = myproc();
	if (user_dst) {
		return copyout(p->pagetable, dst, src, len);
	} else {
		memmove((char *)dst, src, len);
		return 0;
	}
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
	struct proc *p = myproc();
	if (user_src) {
		return copyin(p->pagetable, dst, src, len);
	} else {
		memmove(dst, (char*)src, len);
		return 0;
	}
}
