/*
 * 进程 / 内核线程：PCB、就绪队列、scheduler + swtch 切换。
 */
#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "list.h"
#include "proc.h"
#include "proc_lock.h"
#include "task_queue.h"
#include "spinlock.h"
#include "timer.h"
#include "mmu.h"
#include "x86.h"
#include "debug.h"
#include "gdt.h"

struct proc *proc_table;
struct proc *initproc;
struct proc *userinit;	/* 用户态 init（类似 Linux pid 1），孤儿收养目标 */

/* trap 路径在返回 ring3 前据此恢复用户 CR3 */
pagetable_t current_user_pgdir;

struct spinlock proc_lock;
static int nextpid = 1;

struct cpu cpus[NCPU];

/* 分配新 pid；调用方须持有 proc_lock */
static int alloc_pid(void)
{
	return nextpid++;
}

const char *procstate_str[] = {
#define X(name) #name,
	PROCSTATE_LIST
#undef X
};

extern void swtch(struct context *old, struct context *new);
extern void user_enter(struct trapframe *tf, pagetable_t pgdir) __attribute__((noreturn));
extern char loader[];
extern char loader_end[];
extern char initcode[];
extern char initcode_end[];
extern char end[];
extern void trap_user_return(void);

static int esp_on_kstack(struct proc *p, uint esp);

static void proc_name_set(char *dst, const char *src)
{
	int i;

	for (i = 0; i < NNAME - 1 && src[i]; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

struct proc *myproc(void)
{
	return mycpu()->proc;
}

/* 线程已退出，在 scheduler 上下文回收（swtch 期间仍占用 kstack） */
static void kthread_reap(struct proc *p)
{
	if (p->kstack) {
		free_page(p->kstack);
		p->kstack = 0;
	}
}

/* 用户进程 ZOMBIE：exit 已释放页表；调用方须持有 proc_lock */
static void zombie_reap_locked(struct proc *p)
{
	if (p->state != ZOMBIE)
		return;

	task_queue_unlink_locked(p);
	if (p->kstack) {
		free_page(p->kstack);
		p->kstack = 0;
	}

	memset(&p->context, 0, sizeof(p->context));
	p->state = UNUSED;
	p->pid = 0;
	p->parent = 0;
	p->xstate = 0;
	p->kframe = 0;
	p->pagetable = 0;
	p->sz = 0;
	p->name[0] = '\0';
}

/* 孤儿进程的收养者：优先用户 init，否则内核 init（类似 Linux reparent 到 pid 1） */
static struct proc *child_reaper(void)
{
	if (userinit && userinit->state != UNUSED && userinit->state != ZOMBIE)
		return userinit;
	return initproc;
}

/* 将子进程的父进程改挂到 init（用户或内核） */
static void reparent(struct proc *p)
{
	int i;
	struct proc *cp;
	struct proc *reaper = child_reaper();

	for (i = 0; i < NPROC; i++) {
		cp = &proc_table[i];
		if (cp->parent == p && cp->state != UNUSED)
			cp->parent = reaper;
	}
}

/*
 * 终止当前进程；不返回。资源分阶段释放：
 *   exit      — 用户页表、ZOMBIE 状态、reparent 孤儿、唤醒父进程
 *   wait      — 父进程 waitpid / init_wait_children 回收 kstack 与 PCB
 */
void exit(int status)
{
	struct proc *p = myproc();
	struct proc *parent;

	if (p == initproc)
		panic("init exiting");
	if (p == userinit)
		panic("user init exiting");	/* 类似 Linux：pid 1 不能 exit */

	if (p->pagetable) {
		if (current_user_pgdir == p->pagetable)
			current_user_pgdir = 0;
		uvmfree(p->pagetable);
		p->pagetable = 0;
		p->sz = 0;
	}

	acquire(&proc_lock);
	parent = p->parent;
	reparent(p);
	p->xstate = status;
	task_queue_unlink_locked(p);		/* 抢占 yield 后 exit 时可能仍在就绪队列 */
	p->swtched = 0;
	memset(&p->context, 0, sizeof(p->context));
	p->state = ZOMBIE;
	release(&proc_lock);

	/* 唤醒父进程 */
	if (parent)
		wakeup(parent);

	printf("exit: pid=%d status=%d\n", p->pid, status);
	sched();
	for (;;)
		halt();
}

void context_switch(struct context *old, struct context *new)
{
	intr_off();
	swtch(old, new);
}

void sched(void)
{
	struct proc *p = myproc();
	struct cpu *c = mycpu();
	int iflag = intr_get();
	static struct context zombie_ctx;

	if (c->context.esp == 0)
		panic("sched: scheduler not initialized, "
		      "first sched() should be called by scheduler\n");

	c->last_sched = p;
	if (p->state == ZOMBIE || p->state == UNUSED)
		context_switch(&zombie_ctx, &c->context);
	else
		context_switch(&p->context, &c->context);

	/* 已 exit 的进程不应再次被调度 */
	if (p->state == ZOMBIE)
		for (;;)
			halt();
	if (iflag)
		intr_on();
}

void yield(void)
{
	struct proc *p = myproc();

	mycpu()->need_resched = 0;
	acquire(&proc_lock);
	p->state = RUNNABLE;
	p->swtched = 1;
	task_queue_enqueue_locked(p);
	release(&proc_lock);
	sched();
}

static int should_preempt(void)
{
	struct cpu *c = mycpu();
	struct proc *p;

	/* 持有 spinlock (c->noff != 0) 时，不抢占 */
	if (!c->need_resched || c->noff != 0)
		return 0;
	p = myproc();
	if (!p || p->state != RUNNING)
		return 0;
	acquire(&proc_lock);
	if (task_queue_empty_locked()) {
		c->need_resched = 0;
		release(&proc_lock);
		return 0;
	}
	release(&proc_lock);
	return 1;
}

void preempt_check(void)
{
	struct proc *p = myproc();

	/*
	 * 仅在当前 ESP 落在此进程 kstack 时才允许抢占。
	 * 否则可能正运行在 scheduler 的 kernel_stack / interrupt_stack，
	 * 此时 yield 会把错误的 esp 写入 PCB（导致后续 page fault）。
	 */
	if (!p || !esp_on_kstack(p, r_esp()))
		return;
	if (should_preempt())
		yield();
}

void sched_tick(void)
{
	unsigned int now = timer_ticks();
	int i;
	struct proc *p;

	/* 唤醒睡眠进程 */
	acquire(&proc_lock);
	for (i = 0; i < NPROC; i++) {
		p = &proc_table[i];
		if (p->state != SLEEPING)
			continue;
		if (p->wakeup_tick != 0 && now >= p->wakeup_tick) {
			p->state = RUNNABLE;
			p->wakeup_tick = 0;
			p->chan = 0;
			task_queue_enqueue_locked(p);
		}
	}
	release(&proc_lock);

	/* 检查是否需要抢占 */
	p = myproc();
	if (p && p->state == RUNNING) {
		acquire(&proc_lock);
		if (!task_queue_empty_locked())
			mycpu()->need_resched = 1;
		release(&proc_lock);
	}
}

/**
 * kthread_trampoline - Trampoline 字面意思是「蹦床」。在系统编程里，它指一段很小的
 * 中间代码：不是你真正想跑的业务逻辑，而是帮你「弹」到正确入口、并在结束时收尾的垫层。
 */
static void kthread_trampoline(void)
{
	struct proc *p = myproc();
	void (*fn)(void *) = p->entry;
	void *arg = p->entry_arg;

	fn(arg);
	kthread_exit();
}

/* 内核线程的 context 初始化 */
static void kthread_ctx_init(struct proc *p)
{
	uint *sp;

	memset(&p->context, 0, sizeof(p->context));
	sp = (uint *)((char *)p->kstack + KSTACKSIZE);

	/* 伪造的返回地址, 后面 swtch 用 ret 跳过去, 跳到 kthread_trampoline */
	*--sp = (uint)kthread_trampoline;
	p->context.eip = (uint)kthread_trampoline;
	p->context.esp = (uint)sp;
	p->swtched = 0;
}

/*
 * scheduler 首次调度用户进程时的入口：切页表 + iret 进 ring3。
 * 被抢占后由 timer/syscall 保存的 trapframe 经 iret 恢复，不再经过此处。
 */
static void user_start_trampoline(void)
{
	struct proc *p = myproc();

	if (!p->pagetable || !p->kframe)
		panic("user_start: no user image");

	current_user_pgdir = p->pagetable;

	/* 设置 TSS 的 ESP0 为当前进程的内核栈栈顶 */
	tss_set_esp0((uint)p->kstack + KSTACKSIZE);

	printf("user_start: pid=%d eip=0x%x esp=0x%x pgdir=%p\n",
	       p->pid, p->kframe->eip, p->kframe->esp, p->pagetable);

	user_enter(p->kframe, p->pagetable);
}

/* 用户进程的 context 初始化 */
static void user_ctx_init(struct proc *p)
{
	uint *sp;

	memset(&p->context, 0, sizeof(p->context));

	/* 内核栈顶留给 trapframe；context 栈在其下方 */
	sp = (uint *)((char *)p->kstack + KSTACKSIZE - sizeof(struct trapframe));
	*--sp = (uint)user_start_trampoline;
	p->context.eip = (uint)user_start_trampoline;
	p->context.esp = (uint)sp;
	p->swtched = 0;
}

static int esp_on_kstack(struct proc *p, uint esp)
{
	uint lo = (uint)p->kstack;

	if (!p->kstack)
		return 0;
	return esp >= lo && esp < lo + KSTACKSIZE;
}

/* 指针落在上内核映像内 */
static int on_kernel_text(uint addr)
{
	return addr >= KERNBASE && addr < (uint)end;
}

/* swtch 恢复用的 esp 须落在此进程 kstack 内 */
static int context_on_kstack(struct proc *p)
{
	uint esp = p->context.esp;
	uint lo = (uint)p->kstack;

	if (!p->kstack)
		return 0;
	return esp >= lo && esp < lo + KSTACKSIZE;
}

/*
 * sched 保存的 context 须满足：
 * - esp 在本进程 kstack
 * - eip 为内核地址
 * - 栈顶保存的返回地址与 eip 一致（swtch 约定）
 */
static int context_sane(struct proc *p)
{
	uint eip = p->context.eip;
	uint retaddr;

	if (!context_on_kstack(p))
		return 0;
	if (eip == (uint)user_start_trampoline)
		return 1;
	/* fork 子进程：esp 指向 kframe，eip 为 trap_user_return */
	if (eip == (uint)trap_user_return)
		return p->kframe != 0 && context_on_kstack(p);
	if (!on_kernel_text(eip))
		return 0;
	retaddr = *(uint *)p->context.esp;
	return retaddr == eip;
}

/*
 * 调度前准备 context：
 *   用户进程 swtched=0 → 走 user_start_trampoline
 *   用户进程 swtched=1 → 校验后 swtch 恢复
 *   内核线程           → 校验 kstack，异常则重建
 */
static void prepare_context(struct proc *p)
{
	if (!p->kstack)
		panic("prepare_context: no kstack");

	/* 包含用户态页表的进程 */
	if (p->pagetable) {
		if (!p->swtched) {
			user_ctx_init(p);
		} else if (!context_sane(p)) {
			/* context 损坏但 trapframe 仍有效时，经 user_start 恢复用户态 */
			if (p->kframe && (p->kframe->cs & DPL_USER))
				user_ctx_init(p);
			else
				panic("prepare_context: bad user context");
		}
		return;
	}

	/* 内核线程 */
	if (p->entry && !context_on_kstack(p))
		kthread_ctx_init(p);
}

void procinit(void)
{
	int i;
	struct proc *swapper;

	initlock(&proc_lock, "proc");
	task_queue_init();
	proc_table = kcalloc(NPROC, sizeof(struct proc));
	if (!proc_table)
		panic("procinit: out of memory");

	for (i = 0; i < NPROC; i++) {
		struct proc *p = &proc_table[i];

		INIT_LIST_HEAD(&p->list);
		p->state = UNUSED;
	}

	/*
	 * pid 0 swapper：main 在进 scheduler 前借用此 PCB，不分配 kstack、
	 * 不入就绪队列；scheduler 启动后仅 init/其它线程被调度。
	 */
	swapper = &proc_table[0];
	memset(swapper, 0, sizeof(*swapper));
	INIT_LIST_HEAD(&swapper->list);
	swapper->state = USED;
	swapper->pid = 0;
	swapper->parent = swapper;
	proc_name_set(swapper->name, "swapper|idle");
	mycpu()->proc = swapper;

	printf("proc: table ready, nproc=%d (%d bytes), swapper pid=0\n",
	       NPROC, NPROC * (uint)sizeof(struct proc));
}

void sleep(void *chan)
{
	struct proc *p = myproc();

	if (!chan)
		panic("sleep: null chan");

	acquire(&proc_lock);
	p->chan = chan;
	p->wakeup_tick = 0;
	p->swtched = 1;
	p->state = SLEEPING;
	release(&proc_lock);
	sched();
}

void wakeup(void *chan)
{
	int i;
	struct proc *p;

	if (!chan)
		return;

	acquire(&proc_lock);
	for (i = 0; i < NPROC; i++) {
		p = &proc_table[i];
		if (p->state == SLEEPING && p->chan == chan) {
			p->state = RUNNABLE;
			p->chan = 0;
			p->wakeup_tick = 0;
			task_queue_enqueue_locked(p);
		}
	}
	release(&proc_lock);
}

void sleep_deadline(unsigned int deadline)
{
	struct proc *p = myproc();

	if (timer_ticks() >= deadline)
		return;

	acquire(&proc_lock);
	p->wakeup_tick = deadline;
	p->chan = 0;
	p->swtched = 1;
	p->state = SLEEPING;
	release(&proc_lock);
	sched();
}

void sleep_ticks(unsigned int nticks)
{
	unsigned int now;

	if (nticks == 0)
		return;
	now = timer_ticks();
	sleep_deadline(now + nticks);
}

struct proc *proc_alloc(void)
{
	int i;
	struct proc *p;

	acquire(&proc_lock);
	for (i = 1; i < NPROC; i++) {
		p = &proc_table[i];
		if (p->state != UNUSED)
			continue;

		if (on_list(p))
			panic("proc_alloc: proc on list");

		memset(p, 0, sizeof(*p));
		INIT_LIST_HEAD(&p->list);
		p->state = USED;
		p->pid = alloc_pid();
		release(&proc_lock);
		return p;
	}
	release(&proc_lock);
	return 0;
}

void proc_free(struct proc *p)
{
	if (!p || p->state == UNUSED)
		return;

	acquire(&proc_lock);
	if (p->pagetable) {
		uvmfree(p->pagetable);
		p->pagetable = 0;
		p->sz = 0;
	}
	if (p->kstack) {
		free_page(p->kstack);
		p->kstack = 0;
	}
	memset(&p->context, 0, sizeof(p->context));
	task_queue_unlink_locked(p);
	p->state = UNUSED;
	p->pid = 0;
	p->parent = 0;
	p->kframe = 0;
	p->xstate = 0;
	p->entry = 0;
	p->entry_arg = 0;
	p->name[0] = '\0';
	release(&proc_lock);
}

/* 创建内核线程 */
struct proc *kthread_create(void (*fn)(void *), void *arg, const char *name)
{
	struct proc *p;

	p = proc_alloc();
	if (!p)
		return 0;

	p->kstack = alloc_page();
	if (!p->kstack) {
		proc_free(p);
		return 0;
	}

	p->entry = fn;
	p->entry_arg = arg;

	if (name)
		proc_name_set(p->name, name);
	if (initproc)
		p->parent = initproc;

	kthread_ctx_init(p);

	acquire(&proc_lock);
	p->state = RUNNABLE;
	task_queue_enqueue_locked(p);
	release(&proc_lock);
	return p;
}

/* 退出内核线程 */
void kthread_exit(void)
{
	struct proc *p = myproc();

	if (p == initproc)
		panic("init exiting");

	acquire(&proc_lock);
	p->entry = 0;
	p->entry_arg = 0;
	p->state = UNUSED;
	p->pid = 0;
	p->name[0] = '\0';
	release(&proc_lock);
	sched();
	panic("kthread_exit: sched returned");
}

struct proc *proc_find(int pid)
{
	int i;

	acquire(&proc_lock);
	for (i = 0; i < NPROC; i++) {
		if (proc_table[i].pid == pid &&
		    proc_table[i].state != UNUSED) {
			struct proc *p = &proc_table[i];

			release(&proc_lock);
			return p;
		}
	}
	release(&proc_lock);
	return 0;
}

void scheduler(void)
{
	struct cpu *c = mycpu();
	struct proc *p;
	struct proc *prev;

	printf("scheduler: starting ...\n");

	c->proc = 0;
	for (;;) {
		sti();
		acquire(&proc_lock);

		p = task_queue_dequeue_locked();
		if (p) {
			if (p->state != RUNNABLE)
				panic("scheduler: dequeued non-runnable proc");

			p->state = RUNNING;
			c->proc = p;
			if (p->kstack)
				tss_set_esp0((uint)p->kstack + KSTACKSIZE);
			/* fork 子进程首次调度时尚未经过 ring3 trap，须在此更新 */
			if (p->pagetable)
				current_user_pgdir = p->pagetable;

			prepare_context(p);
			release(&proc_lock);

			context_switch(&c->context, &p->context);

			c->proc = 0;
			prev = c->last_sched;
			if (prev) {
				/*
				 * 用户进程 ZOMBIE 由父进程 wait 回收（类似 Linux）；
				 * 内核线程退出为 UNUSED，在此回收 kstack。
				 */
				if (prev->state == UNUSED)
					kthread_reap(prev);
			}
			c->last_sched = 0;
		} else {
			release(&proc_lock);
			halt();
		}
	}
}

/* fork 子进程首次调度：swtch 到 trap_user_return，iret 恢复 trapframe */
static void fork_user_ctx_init(struct proc *p)
{
	memset(&p->context, 0, sizeof(p->context));
	p->context.eip = (uint)trap_user_return;
	/* esp 须指向 trapframe 起点（非其下方的伪造返回地址） */
	p->context.esp = (uint)p->kframe;
	p->swtched = 1;
}

/*
 * 创建用户 init（pid 通常为 2）：直接加载 initcode，常驻 fork/wait 循环。
 * 由内核 init 线程调用一次；之后由 scheduler 经 user_start 进入 ring3。
 */
void uthread_create_init(void)
{
	struct proc *p;
	struct trapframe *tf;
	void *ustack;
	uint init_sz;

	p = proc_alloc();
	if (!p)
		panic("uthread_create_init: proc_alloc");

	proc_name_set(p->name, "init");
	p->parent = initproc;

	/* 创建进程内核栈，范围 [p->kstack, p->kstack+PGSIZE)；靠恒等映射，无需再映射 */
	p->kstack = alloc_page();
	if (!p->kstack)
		panic("uthread_create_init: kstack");

	/* 创建用户页表 */
	p->pagetable = uvmcreate();
	if (!p->pagetable)
		panic("uthread_create_init: uvmcreate");

	init_sz = (uint)(initcode_end - initcode);
	if (loaduvm(p->pagetable, USERBASE, initcode, init_sz) < 0)
		panic("uthread_create_init: loaduvm");

	/* 创建并映射用户栈 [USERSTACK-PGSIZE, USERSTACK) */
	ustack = alloc_page();
	if (!ustack)
		panic("uthread_create_init: ustack");

	if (uvmmap(p->pagetable, USERSTACK - PGSIZE, (uint)ustack,
		   PGSIZE, PTE_W | PTE_P) < 0)
		panic("uthread_create_init: uvmmap stack");

	p->sz = USERSTACK;

	/* trapframe 位于内核栈顶，供首次 iret 与后续 syscall 使用 */
	tf = (struct trapframe *)((char *)p->kstack + KSTACKSIZE -
				  sizeof(struct trapframe));
	memset(tf, 0, sizeof(*tf));
	tf->eip = USERBASE;	/* 用户程序入口 */
	tf->cs = SEG_UCODE | DPL_USER;
	tf->esp = USERINITESP;
	tf->ss = SEG_UDATA | DPL_USER;
	tf->eflags = 0x202;
	p->kframe = tf;

	/* 初始化用户进程 context，首次调度走 user_start_trampoline */
	user_ctx_init(p);

	acquire(&proc_lock);
	p->state = RUNNABLE;
	task_queue_enqueue_locked(p);
	release(&proc_lock);

	userinit = p;
	printf("uthread_create_init: pid=%d user init queued\n", p->pid);
}

/*
 * fork 系统调用核心：复制页表与 trapframe，子进程 eax=0。
 * 父进程返回子 pid；子进程经 fork_user_ctx_init → trap_user_return 回到用户态。
 */
int fork_copy(struct trapframe *tf)
{
	struct proc *p = myproc();
	struct proc *np;
	struct trapframe *ntf;

	if (!p || !p->pagetable || !tf)
		return -1;

	np = proc_alloc();
	if (!np)
		return -1;

	np->pagetable = uvmcreate();
	if (!np->pagetable)
		goto bad;

	if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
		goto bad;

	np->sz = p->sz;
	np->parent = p;
	proc_name_set(np->name, p->name);

	np->kstack = alloc_page();
	if (!np->kstack)
		goto bad;

	ntf = (struct trapframe *)((char *)np->kstack + KSTACKSIZE -
				   sizeof(struct trapframe));
	{
		uint n;
		char *d = (char *)ntf;
		char *s = (char *)tf;

		for (n = 0; n < sizeof(*ntf); n++)
			d[n] = s[n];
	}
	ntf->eax = 0;	/* 子进程 fork 返回 0 */
	np->kframe = ntf;

	fork_user_ctx_init(np);

	acquire(&proc_lock);
	np->state = RUNNABLE;
	task_queue_enqueue_locked(np);
	release(&proc_lock);

	return np->pid;

bad:
	proc_free(np);
	return -1;
}

/*
 * waitpid 内核实现：扫描 parent 的子进程，回收 ZOMBIE 并返回 pid。
 * pid==-1 表示任意子进程；无子进程返回 -1；有子进程但未退出则 sleep(parent)。
 */
int wait_child(int pid, int *status, struct proc *parent)
{
	struct proc *np;
	int i, havekids, found, xst;

	if (!parent)
		return -1;

	for (;;) {
		acquire(&proc_lock);
		found = 0;
		havekids = 0;
		for (i = 0; i < NPROC; i++) {
			np = &proc_table[i];
			if (np->parent != parent)
				continue;
			if (np->state == UNUSED)
				continue;
			havekids = 1;
			if (np->state != ZOMBIE)
				continue;
			if (pid != -1 && np->pid != pid)
				continue;
			found = np->pid;
			xst = np->xstate;
			zombie_reap_locked(np);
			release(&proc_lock);
			if (status)
				*status = xst;
			return found;
		}
		if (!havekids) {
			release(&proc_lock);
			return -1;
		}
		release(&proc_lock);
		sleep(parent);
	}
}

/*
 * 内核 init 线程等待子进程退出并回收 ZOMBIE；无存活子进程时返回。
 * 子进程 exit() 通过 wakeup(initproc) 唤醒；用户 init 子进程由其自身 waitpid 回收。
 */
void init_wait_children(void)
{
	struct proc *p = myproc();
	int i, havekids;

	for (;;) {
		acquire(&proc_lock);

		/* 回收父进程为 init 的 ZOMBIE 状态的子进程 */
		for (i = 0; i < NPROC; i++) {
			struct proc *ch = &proc_table[i];

			if (ch->parent != initproc || ch->state != ZOMBIE)
				continue;
			printf("init: reaped pid=%d status=%d\n",
			       ch->pid, ch->xstate);
			zombie_reap_locked(ch);
		}
		havekids = 0;

		/* 检查是否还有存活子进程 */
		for (i = 0; i < NPROC; i++) {
			struct proc *ch = &proc_table[i];

			if (ch->parent == initproc && ch->state != UNUSED &&
			    ch->state != ZOMBIE)
				havekids = 1;
		}

		/* 没有存活子进程，返回 */
		if (!havekids) {
			release(&proc_lock);
			return;
		}

		/* 有存活子进程，将唤醒通道设置为 init，并进入睡眠状态 */
		p->chan = initproc;
		p->swtched = 1;
		p->state = SLEEPING;
		release(&proc_lock);
		sched();
	}
}

void dump_runqueue(void)
{
	acquire(&proc_lock);
	task_queue_dump_locked();
	release(&proc_lock);
}