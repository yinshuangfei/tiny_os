/*
 * 进程 / 内核线程：PCB、就绪队列、scheduler + swtch 切换。
 */
#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "list.h"
#include "proc.h"
#include "spinlock.h"
#include "timer.h"
#include "mmu.h"
#include "x86.h"
#include "debug.h"

struct proc *proc_table;
struct proc *initproc;
struct list_head runqueue;

/* trap 路径在返回 ring3 前据此恢复用户 CR3 */
pagetable_t current_user_pgdir;

static struct spinlock proc_lock;
static int nextpid = 1;

const char *procstate_str[] = {
#define X(name) #name,
	PROCSTATE_LIST
#undef X
};

extern void swtch(struct context *old, struct context *new);
extern void user_enter(struct trapframe *tf, pagetable_t pgdir) __attribute__((noreturn));
extern char loader[];
extern char loader_end[];

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

/* 将进程加入就绪队列尾部 */
static void enqueue_runnable(struct proc *p)
{
	if (p->state != RUNNABLE)
		panic("enqueue: not runnable");
	list_add_tail(&p->list, &runqueue);
}

/* 从就绪队列头部取出进程 */
static struct proc *dequeue_runnable(void)
{
	struct list_head *node;
	struct proc *p;

	if (list_empty(&runqueue))
		return 0;
	node = runqueue.next;
	p = list_entry(node, struct proc, list);
	list_del(&p->list);
	INIT_LIST_HEAD(&p->list);
	return p;
}

/* 线程已退出，在 scheduler 上下文回收（swtch 期间仍占用 kstack） */
static void kthread_reap(struct proc *p)
{
	if (p->kstack) {
		free_page(p->kstack);
		p->kstack = 0;
	}
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

	if (c->context.esp == 0)
		panic("sched: scheduler not initialized, "
		      "first sched() should be called by scheduler\n");

	context_switch(&p->context, &c->context);
	if (iflag)
		intr_on();
}

void yield(void)
{
	struct proc *p = myproc();

	mycpu()->need_resched = 0;
	acquire(&proc_lock);
	p->state = RUNNABLE;
	enqueue_runnable(p);
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
	if (list_empty(&runqueue)) {
		c->need_resched = 0;
		release(&proc_lock);
		return 0;
	}
	release(&proc_lock);
	return 1;
}

void preempt_check(void)
{
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
			enqueue_runnable(p);
		}
	}
	release(&proc_lock);

	/* 检查是否需要抢占 */
	p = myproc();
	if (p && p->state == RUNNING) {
		acquire(&proc_lock);
		if (!list_empty(&runqueue))
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

static void kthread_ctx_init(struct proc *p, uint entry)
{
	uint *sp;

	memset(&p->context, 0, sizeof(p->context));
	sp = (uint *)((char *)p->kstack + KSTACKSIZE);
	// 伪造的返回地址, 后面 swtch 用 ret 跳过去, 跳到 kthread_trampoline
	*--sp = entry;
	p->context.eip = entry;
	p->context.esp = (uint)sp;
}

void procinit(void)
{
	int i;
	struct proc *swapper;

	initlock(&proc_lock, "proc");
	INIT_LIST_HEAD(&runqueue);
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
			enqueue_runnable(p);
		}
	}
	release(&proc_lock);
}

void sleep_ticks(unsigned int nticks)
{
	struct proc *p = myproc();

	if (nticks == 0)
		return;

	acquire(&proc_lock);
	p->wakeup_tick = timer_ticks() + nticks;
	p->chan = 0;
	p->state = SLEEPING;
	release(&proc_lock);
	sched();
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

		memset(p, 0, sizeof(*p));
		INIT_LIST_HEAD(&p->list);
		p->state = USED;
		p->pid = nextpid++;
		release(&proc_lock);
		return p;
	}
	release(&proc_lock);
	return 0;
}

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

	kthread_ctx_init(p, (uint)kthread_trampoline);

	acquire(&proc_lock);
	p->state = RUNNABLE;
	enqueue_runnable(p);
	release(&proc_lock);
	return p;
}

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

void proc_free(struct proc *p)
{
	if (!p || p->state == UNUSED)
		return;

	acquire(&proc_lock);
	if (p->kstack) {
		free_page(p->kstack);
		p->kstack = 0;
	}
	p->state = UNUSED;
	p->pid = 0;
	p->parent = 0;
	p->pagetable = 0;
	p->entry = 0;
	p->entry_arg = 0;
	p->name[0] = '\0';
	list_del(&p->list);
	INIT_LIST_HEAD(&p->list);
	release(&proc_lock);
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

	printf("scheduler: starting ...\n");

	c->proc = 0;
	for (;;) {
		sti();
		acquire(&proc_lock);
		p = dequeue_runnable();
		if (p) {
			p->state = RUNNING;
			c->proc = p;
			if (p->kstack)
				tss_set_esp0((uint)p->kstack + KSTACKSIZE);
			release(&proc_lock);
			context_switch(&c->context, &p->context);
			c->proc = 0;
			if (p->state == UNUSED)
				kthread_reap(p);
		} else {
			release(&proc_lock);
			halt();
		}
	}
}

/*
 * 创建第一个用户进程并通过 iret 切入 ring3。
 * 由 init 内核线程调用；正常情况不返回。
 */
void start_first_user(void)
{
	struct proc *p;
	struct trapframe *tf;
	void *ustack;
	uint init_sz;

	p = proc_alloc();
	if (!p)
		panic("start_first_user: proc_alloc");

	proc_name_set(p->name, "loader");
	p->parent = initproc;

	p->kstack = alloc_page();
	if (!p->kstack)
		panic("start_first_user: kstack");

	p->pagetable = uvmcreate();
	if (!p->pagetable)
		panic("start_first_user: uvmcreate");

	init_sz = (uint)(loader_end - loader);
	if (loaduvm(p->pagetable, USERBASE, loader, init_sz) < 0)
		panic("start_first_user: loaduvm");

	ustack = alloc_page();
	if (!ustack)
		panic("start_first_user: ustack");
	if (uvmmap(p->pagetable, USERSTACK - PGSIZE, (uint)ustack,
		   PGSIZE, PTE_W | PTE_P) < 0)
		panic("start_first_user: uvmmap stack");

	p->sz = USERSTACK;

	/* trapframe 放在内核栈顶，供 syscall / 异常处理与 iret 返回 */
	tf = (struct trapframe *)((char *)p->kstack + KSTACKSIZE -
				  sizeof(struct trapframe));
	memset(tf, 0, sizeof(*tf));
	tf->eip = USERBASE;
	tf->cs = SEG_UCODE | 3;
	tf->esp = USERSTACK;
	tf->ss = SEG_UDATA | 3;
	tf->eflags = 0x202;	/* IF | 保留位 1 */
	p->kframe = tf;

	p->state = RUNNING;
	mycpu()->proc = p;
	tss_set_esp0((uint)p->kstack + KSTACKSIZE);
	current_user_pgdir = p->pagetable;

	printf("init: user mode eip=0x%x esp=0x%x pgdir=%p\n",
	       tf->eip, tf->esp, p->pagetable);
	user_enter(tf, p->pagetable);
}
