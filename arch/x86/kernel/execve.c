/*
 * execve：将当前进程映像替换为内核嵌入的 flat binary
 *（含 .text/.rodata/.data/.bss，由 user/user.ld 布局）。
 */
#include "printk.h"
#include "types.h"
#include "defs.h"
#include "param.h"
#include "mm/memlayout.h"
#include "mm/mmu.h"
#include "proc.h"
#include "execve.h"
#include "x86.h"
#include "gdt.h"
#include "fs/fs.h"

extern char init[];
extern char init_end[];
extern char sh[];
extern char sh_end[];

struct userbin {
	const char *name;
	const void *blob;
	uint size;
};

static struct userbin userbins[] = {
	{ "init", init, 0 },
	{ "sh", sh, 0 },
};

static int streq(const char *a, const char *b)
{
	while (*a && *b) {
		if (*a != *b)
			return 0;
		a++;
		b++;
	}
	return *a == *b;
}

static const struct userbin *userbin_lookup(const char *path)
{
	const char *name = path;
	uint i;

	if (path[0] == '/')
		name = path + 1;

	for (i = 0; i < sizeof(userbins) / sizeof(userbins[0]); i++) {
		if (userbins[i].size == 0) {
			if (userbins[i].blob == init)
				userbins[i].size = (uint)(init_end - init);
			else if (userbins[i].blob == sh)
				userbins[i].size = (uint)(sh_end - sh);
		}
		if (streq(name, userbins[i].name))
			return &userbins[i];
	}
	return 0;
}

static void proc_name_from_path(struct proc *p, const char *path)
{
	const char *s;
	const char *last;

	last = path;
	for (s = path; *s; s++) {
		if (*s == '/')
			last = s + 1;
	}
	for (s = last; *s && (s - last) < NNAME - 1; s++)
		p->name[s - last] = *s;
	p->name[s - last] = '\0';
}

/*
 * 在新页表加载 blob + 用户栈；成功时切换 p->pagetable 并更新 trapframe。
 * 失败时释放 newpg，旧映像保持不变。
 */
int exec_load(struct proc *p, struct trapframe *tf, const void *blob, uint size,
	      const char *name)
{
	pagetable_t oldpg, newpg;
	void *ustack;

	if (!p || !tf || !blob || size == 0 || size > USEREND - USERBASE)
		return -1;

	newpg = uvmcreate();
	if (newpg == 0)
		return -1;

	if (loaduvm(newpg, USERBASE, blob, size) < 0)
		goto bad;

	ustack = alloc_page();
	if (ustack == 0)
		goto bad;
	if (uvmmap(newpg, USERSTACK - PGSIZE, (uint)ustack, PGSIZE,
		   PTE_W | PTE_P) < 0) {
		free_page(ustack);
		goto bad;
	}

	oldpg = p->pagetable;
	p->pagetable = newpg;
	p->sz = USERSTACK;
	proc_name_from_path(p, name);

	tf->eip = USERBASE;
	tf->esp = USERINITESP;
	tf->cs = SEG_UCODE | DPL_USER;
	tf->ss = SEG_UDATA | DPL_USER;
	tf->ds = SEG_UDATA | DPL_USER;	/* trap_user_return / 中断返回时 pop %ds */
	tf->eflags = 0x202;

	current_user_pgdir = newpg;
	uvmfree(oldpg);

	printk(KERN_DEBUG "execve: pid=%d name=%s eip=0x%x\n", p->pid, p->name, tf->eip);
	return 0;

bad:
	uvmfree(newpg);
	return -1;
}

int execve(struct proc *p, struct trapframe *tf, const char *path)
{
	const struct userbin *bin;

	bin = userbin_lookup(path);
	if (bin == 0) {
		printk(KERN_ERR "execve: pid=%d unknown program '%s'\n", p->pid, path);
		return -1;
	}
	return exec_load(p, tf, bin->blob, bin->size, bin->name);
}

/*
 * 内核路径 execve（类似 Linux kernel_execve）：
 * 当前 task 在内核态调用，加载用户映像后 iret 进 ring3，不返回。
 */
void kernel_execve(struct proc *p, const char *path)
{
	struct trapframe *tf;

	if (!p || !p->kstack)
		panic("kernel_execve: bad proc");

	tf = (struct trapframe *)((char *)p->kstack + KSTACKSIZE -
				  sizeof(struct trapframe));
	memset(tf, 0, sizeof(*tf));

	if (execve(p, tf, path) < 0)
		panic("kernel_execve: exec failed");

	p->kframe = tf;
	p->entry = 0;
	p->entry_arg = 0;

	if (!p->ofile[0])
		fd_install_stdio(p);
	if (!p->cwd) {
		p->cwd = fs_namei("/");
		if (!p->cwd)
			panic("kernel_execve: cwd");
	}

	user_enter_ring3(p);
}
