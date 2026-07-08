/*
 * execve：将当前进程映像替换为内核嵌入的 flat binary。
 * 尚无文件系统，按程序名查表（见 userbins[]）。
 */
#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

extern char initcode[];
extern char initcode_end[];
extern char loader[];
extern char loader_end[];

struct userbin {
	const char *name;
	const void *blob;
	uint size;
};

static struct userbin userbins[] = {
	{ "loader",   loader,   0 },
	{ "initcode", initcode, 0 },
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
			if (userbins[i].blob == loader)
				userbins[i].size = (uint)(loader_end - loader);
			else if (userbins[i].blob == initcode)
				userbins[i].size = (uint)(initcode_end - initcode);
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
	tf->esp = USERSTACK;
	tf->cs = SEG_UCODE | 3;
	tf->ss = SEG_UDATA | 3;
	tf->eflags = 0x202;

	current_user_pgdir = newpg;
	uvmfree(oldpg);

	printf("execve: pid=%d name=%s eip=0x%x\n", p->pid, p->name, tf->eip);
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
		printf("execve: pid=%d unknown program '%s'\n", p->pid, path);
		return -1;
	}
	return exec_load(p, tf, bin->blob, bin->size, bin->name);
}
