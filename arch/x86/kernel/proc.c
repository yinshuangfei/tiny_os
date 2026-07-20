/*
 * 进程 / 内核线程：PCB、就绪队列、scheduler + swtch 切换。
 */
#include "printk.h"
#include "types.h"
#include "defs.h"
#include "param.h"
#include "mm/memlayout.h"
#include "list.h"
#include "proc.h"
#include "lock/proc_lock.h"
#include "task_queue.h"
#include "lock/spinlock.h"
#include "timer.h"
#include "mm/mmu.h"
#include "x86.h"
#include "debug.h"
#include "gdt.h"
#include "fs/fs.h"
#include "ipc/signal.h"

/* 进程表 */
struct proc *proc_table;
/* init 进程 */
struct proc *initproc;
/* kthreadd 进程 */
struct proc *kthreadd_task;

/* trap 路径在返回 ring3 前据此恢复用户 CR3 */
pagetable_t current_user_pgdir;

/* 进程表锁 */
struct spinlock proc_lock;
/* 下一个 pid */
static int nextpid = 1;

/*
 * kthreadd 创建请求队列（类似 Linux kthread_create → kthreadd）。
 * 用 proc_lock 保护；调用方 sleep(info) 等待创建完成。
 */
struct kthread_create_info {
	struct list_head list;
	void (*threadfn)(void *);
	void *arg;
	char name[NNAME];
	struct proc *result;	/* 创建的进程 */
	int done;		/* 创建是否完成 */
};

/* kthreadd 创建请求队列, struct kthread_create_info 的链表头 */
static struct list_head kthread_create_list;

/* kthreadd 创建请求队列是否已初始化 */
static int kthread_create_inited;

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

/* 将子进程的父进程改挂到 init（pid 1） */
static void reparent(struct proc *p)
{
	int i;
	struct proc *cp;

	for (i = 0; i < NPROC; i++) {
		cp = &proc_table[i];
		if (cp->parent == p && cp->state != UNUSED)
			cp->parent = initproc;
	}
}

/*
 * 终止当前进程；不返回。资源分阶段释放：
 *   exit      — 用户页表、ZOMBIE 状态、reparent 孤儿、唤醒父进程
 *   wait      — 父进程 waitpid 回收 kstack 与 PCB
 *
 * xstate 为 Linux wait 状态字（见 user/include/sys/wait.h）：
 *   正常退出 (status << 8)；信号致死 (sig & 0x7f)。
 */
static void exit_with_xstate(int xstate) __attribute__((noreturn));

static void exit_with_xstate(int xstate)
{
	struct proc *p = myproc();
	struct proc *parent;

	if (p == initproc)
		panic("init exiting");	/* pid 1 不能 exit（用户态 init 亦同） */

	signal_parent_child_exit(p);
	signal_exit_cleanup(p);

	fd_closeall(p);
	if (p->cwd) {
		fs_iput(p->cwd);
		p->cwd = 0;
	}

	if (p->pagetable) {
		if (current_user_pgdir == p->pagetable)
			current_user_pgdir = 0;
		uvmfree(p->pagetable);
		p->pagetable = 0;
		p->sz = 0;
	}

	fpu_drop(p);

	acquire(&proc_lock);
	parent = p->parent;
	reparent(p);
	p->xstate = xstate;
	task_queue_unlink_locked(p);		/* 抢占 yield 后 exit 时可能仍在就绪队列 */
	memset(&p->context, 0, sizeof(p->context));
	p->state = ZOMBIE;
	release(&proc_lock);

	/* 唤醒父进程 */
	if (parent)
		wakeup(parent);

	printk(KERN_DEBUG "exit: pid=%d xstate=%d\n", p->pid, xstate);
	sched();
	for (;;)
		halt();
}

/* 用户 exit(status)：编码为 WIFEXITED / WEXITSTATUS */
void exit(int status)
{
	exit_with_xstate((status & 0xff) << 8);
}

/* 致命信号默认动作：编码为 WIFSIGNALED / WTERMSIG */
void exit_signal(int sig)
{
	if (sig < 1)
		sig = 1;
	exit_with_xstate(sig & 0x7f);
}

void context_switch(struct context *old, struct context *new)
{
	intr_off();
	fpu_switch_away();
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
}

/*
 * scheduler 首次调度用户进程时的入口：切页表 + iret 进 ring3。
 * 被抢占后由 timer/syscall 保存的 trapframe 经 iret 恢复，不再经过此处。
 * kernel_execve 亦经 user_enter_ring3 直接进入 ring3。
 */
void user_enter_ring3(struct proc *p)
{
	if (!p->pagetable || !p->kframe)
		panic("user_enter_ring3: no user image");

	current_user_pgdir = p->pagetable;
	tss_set_esp0((uint)p->kstack + KSTACKSIZE);

	printk(KERN_DEBUG "user_start: pid=%d eip=0x%x esp=0x%x pgdir=%p\n",
	       p->pid, p->kframe->eip, p->kframe->esp, p->pagetable);

	user_enter(p->kframe, p->pagetable);
}

static void user_start_trampoline(void)
{
	user_enter_ring3(myproc());
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
 * 调度前校验 context（对齐 Linux：创建时已备好可 switch_to 的栈，无 swtched 标志）。
 * 异常时：用户进程若 kframe 仍有效则重建 trampoline；内核线程则重建入口。
 */
static void prepare_context(struct proc *p)
{
	if (!p->kstack)
		panic("prepare_context: no kstack");

	if (context_sane(p))
		return;

	if (p->pagetable) {
		if (p->kframe && (p->kframe->cs & DPL_USER))
			user_ctx_init(p);
		else
			panic("prepare_context: bad user context");
		return;
	}

	if (p->entry)
		kthread_ctx_init(p);
	else
		panic("prepare_context: bad context");
}

void procinit(void)
{
	int i;
	struct proc *swapper;

	initlock(&proc_lock, "proc");
	task_queue_init();

	INIT_LIST_HEAD(&kthread_create_list);
	kthread_create_inited = 1;

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

	printk(KERN_ERR "proc alloc failed, no free proc\n");
	return 0;
}

void proc_free(struct proc *p)
{
	if (!p || p->state == UNUSED)
		return;

	fpu_drop(p);

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

/* 同步创建内核线程（boot / kthreadd 内部使用） */
static struct proc *kthread_create_sync(void (*fn)(void *), void *arg,
					const char *name, struct proc *parent)
{
	struct proc *p;

	p = proc_alloc();
	if (!p) {
		printk(KERN_ERR "kthread_create: no free PCB for '%s'\n",
		       name ? name : "?");
		return 0;
	}

	p->kstack = alloc_page();
	if (!p->kstack) {
		printk(KERN_ERR
		       "kthread_create: no kstack page for '%s' (free=%d)\n",
		       name ? name : "?", pmm_nr_free_pages());
		proc_free(p);
		return 0;
	}

	p->entry = fn;
	p->entry_arg = arg;
	if (name)
		proc_name_set(p->name, name);
	if (parent)
		p->parent = parent;

	kthread_ctx_init(p);

	acquire(&proc_lock);
	p->state = RUNNABLE;
	task_queue_enqueue_locked(p);
	release(&proc_lock);
	return p;
}

/*
 * 经 kthreadd 异步创建：入队请求，唤醒 kthreadd，sleep 等待结果。
 * parent 设为 kthreadd（与 Linux 一致）。
 *
 * 在 proc_lock 下等待 done，避免「创建已完成却尚未 sleep」的丢失唤醒。
 */
static struct proc *kthread_create_via_kthreadd(void (*fn)(void *), void *arg,
					       const char *name)
{
	struct kthread_create_info *info;
	struct proc *p, *cur;

	info = kmalloc(sizeof(*info));
	if (!info) {
		printk(KERN_ERR "kthread_create_via_kthreadd: out of memory");
		return 0;
	}

	memset(info, 0, sizeof(*info));
	INIT_LIST_HEAD(&info->list);

	info->threadfn = fn;
	info->arg = arg;
	if (name)
		proc_name_set(info->name, name);

	acquire(&proc_lock);
	list_add_tail(&info->list, &kthread_create_list);
	/* 若 kthreadd 在睡，先唤醒（持锁外 wakeup 也可，这里先放锁） */
	release(&proc_lock);
	wakeup(&kthread_create_list);

	/* 等待创建完成 */
	cur = myproc();
	acquire(&proc_lock);
	while (!info->done) {
		cur->chan = info;
		cur->wakeup_tick = 0;
		cur->state = SLEEPING;
		release(&proc_lock);
		sched();
		acquire(&proc_lock);
	}
	p = info->result;
	release(&proc_lock);

	kfree(info);
	return p;
}

/* kthreadd（pid 2）：消费创建请求，类似 Linux kthreadd */
void kthreadd(void *arg)
{
	(void)arg;

	printf("kthreadd: pid=%d starting\n", myproc()->pid);

	for (;;) {
		struct kthread_create_info *info;
		struct proc *p;

		acquire(&proc_lock);
		if (list_empty(&kthread_create_list)) {
			release(&proc_lock);
			sleep(&kthread_create_list);
			continue;
		}
		info = list_first_entry(&kthread_create_list,
					struct kthread_create_info, list);
		list_del(&info->list);
		release(&proc_lock);

		p = kthread_create_sync(info->threadfn, info->arg,
					info->name[0] ? info->name : 0,
					kthreadd_task);

		acquire(&proc_lock);
		info->result = p;
		info->done = 1;
		/* 唤醒等待该请求的创建者（chan == info） */
		{
			int i;
			struct proc *w;

			for (i = 0; i < NPROC; i++) {
				w = &proc_table[i];
				if (w->state == SLEEPING && w->chan == info) {
					w->state = RUNNABLE;
					w->chan = 0;
					w->wakeup_tick = 0;
					task_queue_enqueue_locked(w);
				}
			}
		}
		release(&proc_lock);
	}
}

/* 创建内核线程：kthreadd 就绪后走创建队列，否则同步创建（boot 路径） */
struct proc *kthread_create(void (*fn)(void *), void *arg, const char *name)
{
	struct proc *parent;

	if (kthreadd_task)
		return kthread_create_via_kthreadd(fn, arg, name);

	/* boot：init / kthreadd 自身；parent 由调用方再设 */
	parent = initproc ? initproc : 0;
	return kthread_create_sync(fn, arg, name, parent);
}

/* 退出内核线程 */
void kthread_exit(void)
{
	struct proc *p = myproc();

	if (p == initproc)
		panic("init exiting");
	if (p == kthreadd_task)
		panic("kthreadd exiting");

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

			printk(KERN_DEBUG "scheduler: switching to pid=%d\n", p->pid);
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
			printk(KERN_DEBUG "scheduler: no process to switch to, halting\n");
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
}

/*
 * fork 系统调用核心：复制页表与 trapframe，子进程 eax=0。
 * 父进程返回子 pid；子进程经 fork_user_ctx_init → trap_user_return 回到用户态。
 */
int fork_copy(struct trapframe *tf)
{
	struct proc *p = myproc();
	struct proc *np;	/* new process */
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
	np->brk = p->brk;
	np->brk_start = p->brk_start;
	vma_copy(np, p);
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

	fd_copy(np, p);
	if (p->cwd)
		np->cwd = fs_idup(p->cwd);

	signal_fork(np, p);
	fpu_fork(np, p);
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

void dump_runqueue(void)
{
	acquire(&proc_lock);
	task_queue_dump_locked();
	release(&proc_lock);
}