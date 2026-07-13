#include "riscv.h"
#include "proc.h"
#include "defs.h"
#include "spinlock.h"
#include "memlayout.h"
#include "fs.h"

// 每个核上的 CPU 状态数组
struct cpu cpus[NCPU];

// 进程数组
struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern char trampoline[]; // trampoline.S

extern void forkret(void);
static void wakeup1(struct proc *chan);
static void freeproc(struct proc *p);

// initialize the proc table at boot time.
// 设置内核进程栈，设置内核页表
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

		/** 进程内核栈
		 * (int) (p - proc) 的值为: 0 ~ NPROC
		 * 映射到内核页表上
		 */
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

	// 这里是从内核进程中找一个空闲的进程
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
	// p->trapframe 在 proc_pagetable() 中进行了映射
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
	// 指向内核栈地址
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
// 分配用户进程页表
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
// initcode 为链接后的目标文件，非 ELF 文件
// 通过系统调用 exec 加载并执行磁盘上的 /init 程序，从而完成从内核态到用户态应用程序的
// 过渡.
/**
 * initcode 是内核启动后在用户空间运行的第一个程序, 使用机器码是为了避免：
 * - 运行一个普通的 ELF 可执行文件（比如 _init），内核必须拥有完整的文件系统（VFS）和
 *   可执行文件加载器（exec）;
 * - 普通的用户程序（如 ls）都是标准的 ELF 格式, 要运行它们，内核的 exec 系统调用需要：
 * 	- 读取并解析 ELF 头部（Header）和程序头表（Program Header）；
 * 	- 根据段（Segment）信息分配内存；
 * 	- 建立用户页表并映射物理内存；
 * 	- 设置栈和环境变量；
 */
uchar initcode[] = {
	0x17, 0x05, 0x00, 0x00,  // auipc a0, 0x0      (a0 = 当前 PC)
	0x13, 0x05, 0x45, 0x02,  // addi a0, a0, 36    (a0 = PC + 36，指向字符串 "/init")
	0x97, 0x05, 0x00, 0x00,  // auipc a1, 0x0      (a1 = 当前 PC)
	0x93, 0x85, 0x45, 0x02,  // addi a1, a1, 36    (a1 = PC + 36，指向 argv[] 指针数组)
	0x93, 0x08, 0x70, 0x00,  // addi a7, zero, 7   (a7 = 7，即 SYS_exec 系统调用号)
	0x73, 0x00, 0x00, 0x00,  // ecall              (触发陷入内核)
	0x93, 0x08, 0x20, 0x00,  // addi a7, zero, 2   (a7 = 2，即 SYS_exit 系统调用号)
	0x73, 0x00, 0x00, 0x00,  // ecall              (如果 exec 失败，调用 exit 退出)
	0xef, 0xf0, 0x9f, 0xff,  // jal zero, -4       (如果 exit 也失败，跳转到当前指令死循环)
	0x2f, 0x69, 0x6e, 0x69,  // ASCII: "/ini"
	0x74, 0x00, 0x00, 0x24,  // ASCII: "t\0\0$" (字符串 "/init" 及其对齐填充)
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00
};

// 用户态触发一次 ecall，然后进入死循环。
uchar loop_code[] = {
	// ecall
	0x73, 0x00, 0x00, 0x00,
	// j .
	0x6f, 0x00, 0x00, 0x00,
};

// 用户态触发一次 ecall，然后进入 exit。
uchar exit_code[] = {
	// 机器码: addi a0, zero, 0
	0x13, 0x05, 0x00, 0x00,
	// li a7, 02 (设置系统调用号为 02，即 exit)
	// 机器码: addi a7, zero, 02
	0x93, 0x08, 0x20, 0x00,
	// ecall (触发系统调用，进入内核执行退出逻辑)
	0x73, 0x00, 0x00, 0x00,
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
	uvminit(p->pagetable, initcode, sizeof(initcode));
	p->sz = PGSIZE;

	// prepare for the very first "return" from kernel to user.
	p->trapframe->epc = 0;      // user program counter
	p->trapframe->sp = PGSIZE;  // user stack pointer

	safestrcpy(p->name, "initcode", sizeof(p->name));
	p->cwd = namei("/");

	p->state = RUNNABLE;

	release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
	uint sz;
	struct proc *p = myproc();

	sz = p->sz;
	if (n > 0) {
		if ((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
			return -1;
		}
	} else if(n < 0) {
		sz = uvmdealloc(p->pagetable, sz, sz + n);
	}
	p->sz = sz;
	return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int fork(void)
{
	int i, pid;
	struct proc *np;
	struct proc *p = myproc();

	// Allocate new process.
	if ((np = allocproc()) == 0){
		return -1;
	}

	// Copy user memory from parent to child.
	if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
		freeproc(np);
		release(&np->lock);
		return -1;
	}

	np->sz = p->sz;
	np->parent = p;

	// copy saved user registers.
	// trapframe->epc 的值也被复制，所以父子进程的运行点一致
	*(np->trapframe) = *(p->trapframe);

	// Cause fork to return 0 in the child.
	// 子进程返回 0
	np->trapframe->a0 = 0;
	// increment reference counts on open file descriptors.
	for (i = 0; i < NOFILE; i++)
		if (p->ofile[i])
			np->ofile[i] = filedup(p->ofile[i]);

	np->cwd = idup(p->cwd);

	safestrcpy(np->name, p->name, sizeof(p->name));

	pid = np->pid;

	np->state = RUNNABLE;

	release(&np->lock);

	// 父进程直接返回，子进程等待调度运行
	return pid;
}

// Pass p's abandoned children to init.
// Caller must hold p->lock.
void reparent(struct proc *p)
{
	struct proc *pp;

	for (pp = proc; pp < &proc[NPROC]; pp++) {
		// this code uses pp->parent without holding pp->lock.
		// acquiring the lock first could cause a deadlock
		// if pp or a child of pp were also in exit()
		// and about to try to lock p.
		if (pp->parent == p) {
			// pp->parent can't change between the check and the acquire()
			// because only the parent changes it, and we're the parent.
			acquire(&pp->lock);
			pp->parent = initproc;
			// we should wake up init here, but that would require
			// initproc->lock, which would be a deadlock, since we hold
			// the lock on one of init's children (pp). this is why
			// exit() always wakes init (before acquiring any locks).
			release(&pp->lock);
		}
	}
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void exit(int status)
{
	struct proc *p = myproc();

	if (p == initproc)
		panic("init exiting");

	// Close all open files.
	for (int fd = 0; fd < NOFILE; fd++) {
		if (p->ofile[fd]) {
			struct file *f = p->ofile[fd];
			fileclose(f);
			p->ofile[fd] = 0;
		}
	}

	begin_op();
	iput(p->cwd);
	end_op();
	p->cwd = 0;

	// we might re-parent a child to init. we can't be precise about
	// waking up init, since we can't acquire its lock once we've
	// acquired any other proc lock. so wake up init whether that's
	// necessary or not. init may miss this wakeup, but that seems
	// harmless.
	acquire(&initproc->lock);
	wakeup1(initproc);
	release(&initproc->lock);

	// grab a copy of p->parent, to ensure that we unlock the same
	// parent we locked. in case our parent gives us away to init while
	// we're waiting for the parent lock. we may then race with an
	// exiting parent, but the result will be a harmless spurious wakeup
	// to a dead or wrong process; proc structs are never re-allocated
	// as anything else.
	acquire(&p->lock);
	struct proc *original_parent = p->parent;
	release(&p->lock);

	// we need the parent's lock in order to wake it up from wait().
	// the parent-then-child rule says we have to lock it first.
	acquire(&original_parent->lock);

	acquire(&p->lock);

	// Give any children to init.
	reparent(p);

	// Parent might be sleeping in wait().
	wakeup1(original_parent);

	p->xstate = status;
	p->state = ZOMBIE;

	release(&original_parent->lock);

	// Jump into the scheduler, never to return.
	// 父进程释放其资源后，释放 p->lock
	sched();
	panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr)
{
	struct proc *np;
	int havekids, pid;
	struct proc *p = myproc();

	// hold p->lock for the whole time to avoid lost
	// wakeups from a child's exit().
	acquire(&p->lock);

	for (;;) {
		// Scan through table looking for exited children.
		havekids = 0;
		for (np = proc; np < &proc[NPROC]; np++) {
			// this code uses np->parent without holding np->lock.
			// acquiring the lock first would cause a deadlock,
			// since np might be an ancestor, and we already hold p->lock.
			if (np->parent == p) {
				// np->parent can't change between the check and the acquire()
				// because only the parent changes it, and we're the parent.
				acquire(&np->lock);
				havekids = 1;
				if (np->state == ZOMBIE) {
					// Found one.
					pid = np->pid;
					if (addr != 0 &&
					    copyout(p->pagetable, addr, (char *)&np->xstate, sizeof(np->xstate)) < 0) {
						release(&np->lock);
						release(&p->lock);
						return -1;
					}
					freeproc(np);
					release(&np->lock);
					release(&p->lock);
					return pid;
				}
				release(&np->lock);
			}
		}

		// No point waiting if we don't have any children.
		if (!havekids || p->killed) {
			release(&p->lock);
			return -1;
		}

		// Wait for a child to exit.
		sleep(p, &p->lock);  //DOC: wait-sleep
	}
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
		// printf("sche before intr_on, cpu:%d, noff:%d, intena:%d, sstatus:0x%x\n",
		// 	cpuid(), mycpu()->noff, mycpu()->intena, r_sstatus());
		// 开启中断后，触发的中断会进入 kernelvec.S 的 kernelvec 函数
		// 跳转入口在 trapinithart 函数中确定
		intr_on();
		// printf("sche after intr_on, cpu:%d, noff:%d, intena:%d, sstatus:0x%x\n",
		// 	cpuid(), mycpu()->noff, mycpu()->intena, r_sstatus());

		int found = 0;
		for (p = proc; p < &proc[NPROC]; p++) {
			// printf("sche: iter proc\n");
			acquire(&p->lock);
			if (p->state == RUNNABLE) {
				// Switch to chosen process.  It is the process's job
				// to release its lock and then reacquire it
				// before jumping back to us.
				p->state = RUNNING;
				c->proc = p;

				// printf("sche: cpu:%d, schedule into -> '%s'\n", cpuid(), p->name);
				/**
				 * 完成进程切换, 调用汇编代码 swtch.S 中的函数 swtch,
				 * 函数原型为:
				 * - void swtch(struct context *old, struct context *new);
				 * 执行 new->context->ra 指向的函数 forkret().
				 * 特权模式未变.
				 *
				 * 执行了 swtch(*old, *new)；
				 * 当前内核进程的状态保存至 old->context, 摇身一变新进程，
				 **/
				swtch(&c->context, &p->context);
				/** 返回，表示切换回调度程序 */
				// printf("sche: cpu:%d, schedule back\n", cpuid());

				// Process is done running for now.
				// It should have changed its p->state before coming back.
				c->proc = 0;

				found = 1;
			}
			release(&p->lock);
		}
		if (found == 0) {
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
	/** 切换回调度程序，下次切换回来从该函数返回 */
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
// 第一次进入 forkret() 不是通过普通 call forkret，而是通过"把 ra 设成 forkret 然后
// ret"进去的, 目前位于内核态.
void forkret(void)
{
	static int first = 1;

	// Still holding p->lock from scheduler.
	// 调度器查找到该进程时，已经上锁
	release(&myproc()->lock);

	if (first) {
		// File system initialization must be run in the context of a
		// regular process (e.g., because it calls sleep), and thus cannot
		// be run from main().
		first = 0;
		fsinit(ROOTDEV);
	}

	// forkret() 不应该像普通 C 函数那样返回, 它应该转到 usertrapret()，再进入
	// trampoline.S 的 sret 回到用户态。那条路径不会再执行 forkret() 末尾的 ret。
	usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
	struct proc *p = myproc();

	// Must acquire p->lock in order to
	// change p->state and then call sched.
	// Once we hold p->lock, we can be
	// guaranteed that we won't miss any wakeup
	// (wakeup locks p->lock),
	// so it's okay to release lk.
	if (lk != &p->lock) {  //DOC: sleeplock0
		acquire(&p->lock);  //DOC: sleeplock1
		release(lk);
	}

	// Go to sleep.
	p->chan = chan;
	p->state = SLEEPING;

	sched();

	// Tidy up.
	p->chan = 0;

	// Reacquire original lock.
	if (lk != &p->lock) {
		release(&p->lock);
		acquire(lk);
	}
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

// Wake up p if it is sleeping in wait(); used by exit().
// Caller must hold p->lock.
static void wakeup1(struct proc *p)
{
	if (!holding(&p->lock))
		panic("wakeup1");
	if (p->chan == p && p->state == SLEEPING) {
		p->state = RUNNABLE;
	}
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kill(int pid)
{
	struct proc *p;

	for (p = proc; p < &proc[NPROC]; p++) {
		acquire(&p->lock);
		if (p->pid == pid) {
			p->killed = 1;
			if (p->state == SLEEPING) {
				// Wake process from sleep().
				p->state = RUNNABLE;
			}
			release(&p->lock);
			return 0;
		}
		release(&p->lock);
	}
	return -1;
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
