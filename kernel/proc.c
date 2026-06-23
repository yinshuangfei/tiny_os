#include "riscv.h"
#include "proc.h"
#include "defs.h"
#include "spinlock.h"
#include "memlayout.h"
#include "fs.h"

struct cpu cpus[NCPU];

// kernel 进程
struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern char trampoline[]; // trampoline.S

extern void forkret(void);
static void freeproc(struct proc *p);

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
		if (pa == 0)
			panic("kalloc");

		/** 内核进程栈 */
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
	push_off();
	struct cpu *c = mycpu();
	struct proc *p = c->proc;
	pop_off();
	return p;
}

int allocpid()
{
	int pid;

	acquire(&pid_lock);
	pid = nextpid;
	nextpid = nextpid + 1;
	release(&pid_lock);

	return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc* allocproc(void)
{
	struct proc *p;

	for (p = proc; p < &proc[NPROC]; p++) {
		acquire(&p->lock);
		if (p->state == UNUSED) {
			goto found;
		} else {
			release(&p->lock);
		}
	}
	return 0;

found:
	p->pid = allocpid();

	// Allocate a trapframe page.
	if ((p->trapframe = (struct trapframe *)kalloc()) == 0) {
		release(&p->lock);
		return 0;
	}
	// An empty user page table.
	p->pagetable = proc_pagetable(p);
	if (p->pagetable == 0) {
		freeproc(p);
		release(&p->lock);
		return 0;
	}

	// Set up new context to start executing at forkret,
	// which returns to user space.
	memset(&p->context, 0, sizeof(p->context));
	// 指定了运行地址为 forkret
	p->context.ra = (uint64)forkret;
	p->context.sp = p->kstack + PGSIZE;

	return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void freeproc(struct proc *p)
{
	if (p->trapframe)
		kfree((void*)p->trapframe);
	p->trapframe = 0;
	if (p->pagetable)
		proc_freepagetable(p->pagetable, p->sz);
	p->pagetable = 0;
	p->sz = 0;
	p->pid = 0;
	p->parent = 0;
	p->name[0] = 0;
	p->chan = 0;
	p->killed = 0;
	p->xstate = 0;
	p->state = UNUSED;
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
// 用户进程有自己的页表机制
pagetable_t proc_pagetable(struct proc *p)
{
	pagetable_t pagetable;

	// An empty page table.
	pagetable = uvmcreate();
	if (pagetable == 0)
		return 0;

	// map the trampoline code (for system call return)
	// at the highest user virtual address.
	// only the supervisor uses it, on the way
	// to/from user space, so not PTE_U.
	if (mappages(pagetable, TRAMPOLINE, PGSIZE,
		     (uint64)trampoline, PTE_R | PTE_X) < 0) {
		uvmfree(pagetable, 0);
		return 0;
	}

	// map the trapframe just below TRAMPOLINE, for trampoline.S.
	if (mappages(pagetable, TRAPFRAME, PGSIZE,
		     (uint64)(p->trapframe), PTE_R | PTE_W) < 0) {
		uvmunmap(pagetable, TRAMPOLINE, 1, 0);
		uvmfree(pagetable, 0);
		return 0;
	}

	return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
	uvmunmap(pagetable, TRAMPOLINE, 1, 0);
	uvmunmap(pagetable, TRAPFRAME, 1, 0);
	uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
	0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
	0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
	0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
	0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
	0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
	0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00
};

// 用户态触发一次 ecall，由内核打印一行消息。
// 这样不依赖用户态直接访问 UART MMIO。
uchar printcode[] = {
    // ecall
    0x73, 0x00, 0x00, 0x00,

    // j .
    0x6f, 0x00, 0x00, 0x00,
};

// Set up first user process.
void userinit(void)
{
	struct proc *p;

	p = allocproc();
	initproc = p;

	// allocate one user page and copy init's instructions
	// and data into it.
	uvminit(p->pagetable, printcode, sizeof(printcode));
	p->sz = PGSIZE;

	// prepare for the very first "return" from kernel to user.
	p->trapframe->epc = 0;      // user program counter
	p->trapframe->sp = PGSIZE;  // user stack pointer

	safestrcpy(p->name, "initcode", sizeof(p->name));
	// p->cwd = namei("/");

	p->state = RUNNABLE;

	release(&p->lock);
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler(void)
{
	struct proc *p;
	struct cpu *c = mycpu();

	c->proc = 0;
	for (;;) {
		// Avoid deadlock by ensuring that devices can interrupt.
		printf("sche: cpu:%d, noff:%d, intena:%d, sstatus:0x%x\n",
			cpuid(), mycpu()->noff, mycpu()->intena, r_sstatus());
		// 开启中断后，触发的中断会进入 kernelvec.S 的 kernelvec 函数
		// 跳转入口在 trapinithart 函数中确定
		intr_on();
		printf("sche: cpu:%d, noff:%d, intena:%d, sstatus:0x%x\n",
			cpuid(), mycpu()->noff, mycpu()->intena, r_sstatus());

		int found = 0;
		for (p = proc; p < &proc[NPROC]; p++) {
			acquire(&p->lock);
			if (p->state == RUNNABLE) {
				// Switch to chosen process.  It is the process's job
				// to release its lock and then reacquire it
				// before jumping back to us.
				p->state = RUNNING;
				c->proc = p;

				printf("sche: cpu:%d, schedule into -> '%s'\n", cpuid(), p->name);
				/** 完成进程切换, 调用汇编代码 swtch.S */
				swtch(&c->context, &p->context);
				/** 返回，表示切换会调度程序 */
				printf("sche: cpu:%d, schedule back\n", cpuid());

				// Process is done running for now.
				// It should have changed its p->state before coming back.
				c->proc = 0;

				found = 1;
			}
			release(&p->lock);
		}
		if (found == 0) {
			printf("not find proc, wfi\n");
			intr_on();
			/**
			 * 让 CPU 暂停当前的执行流，进入低功耗等待状态，直到有中断
			 * 事件发生将其唤醒.
			 * wfi: Wait For Interrupt（等待中断）
			 **/
			asm volatile("wfi");
		}
	}
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
	int intena;
	struct proc *p = myproc();

	if (!holding(&p->lock))
		panic("sched p->lock");
	if (mycpu()->noff != 1)
		panic("sched locks");
	if (p->state == RUNNING)
		panic("sched running");
	if (intr_get())
		panic("sched interruptible");

	intena = mycpu()->intena;
	swtch(&p->context, &mycpu()->context);
	mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
	struct proc *p = myproc();
	acquire(&p->lock);
	p->state = RUNNABLE;
	sched();
	release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void)
{
	printf("forkret\n");
	static int first = 1;

	// Still holding p->lock from scheduler.
	release(&myproc()->lock);

	if (first) {
		// File system initialization must be run in the context of a
		// regular process (e.g., because it calls sleep), and thus cannot
		// be run from main().
		first = 0;
		fsinit(ROOTDEV);
	}

	usertrapret();
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void wakeup(void *chan)
{
	struct proc *p;

	for (p = proc; p < &proc[NPROC]; p++) {
		acquire(&p->lock);
		if (p->state == SLEEPING && p->chan == chan) {
			p->state = RUNNABLE;
		}
		release(&p->lock);
	}
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
