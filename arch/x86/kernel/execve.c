/*
 * execve：将当前进程映像替换为用户程序。
 * 支持 ELF32（i386 ET_EXEC）与 flat binary（入口 USERBASE）。
 *
 * 用户栈布局对齐 Linux i386 / SysV ABI（create_elf_tables）：
 *   高址 │ 字符串（argv / envp）
 *       │ AT_NULL 辅助向量
 *       │ envp[0..n], NULL
 *       │ argv[0..argc-1], NULL
 *   低址 │ argc                    ← %esp（16 字节对齐）
 *
 * 查找顺序：
 *   1) 文件系统路径（fs_namei + 读入）
 *   2) 内核嵌入表（仅 init/sh 启动用）
 */
#include "printk.h"
#include "types.h"
#include "defs.h"
#include "param.h"
#include "mm/memlayout.h"
#include "mm/mmu.h"
#include "mm/vm.h"
#include "proc.h"
#include "execve.h"
#include "elf.h"
#include "x86.h"
#include "gdt.h"
#include "fs/fs.h"
#include "ipc/signal.h"

extern char init[];
extern char init_end[];
extern char sh[];
extern char sh_end[];

/* 可执行文件读入上限（与用户 VA 窗口同量级） */
#define EXEC_MAX_FILE (USEREND - USERBASE)

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

static uint strlen_u(const char *s)
{
	uint n = 0;

	if (!s)
		return 0;
	while (s[n])
		n++;
	return n;
}

static const struct userbin *userbin_lookup(const char *path)
{
	const char *name = path;
	uint i;

	if (!path)
		return 0;
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

static int is_elf(const void *blob, uint size)
{
	const struct elfhdr *eh;

	if (size < sizeof(struct elfhdr))
		return 0;
	eh = (const struct elfhdr *)blob;
	return eh->magic == ELF_MAGIC;
}

/*
 * 按 PT_LOAD 装入 ELF；成功时 *entry = e_entry，*out_brk = 段末最高地址。
 */
static int exec_load_elf(pagetable_t pgdir, const char *blob, uint size,
			 uint *entry, uint *out_brk)
{
	const struct elfhdr *eh;
	const struct proghdr *ph;
	uint i, ph_end, brk;
	int perm;

	eh = (const struct elfhdr *)blob;
	if (eh->magic != ELF_MAGIC || eh->type != ET_EXEC ||
	    eh->machine != EM_386)
		return -1;
	if (eh->phentsize != sizeof(struct proghdr) || eh->phnum == 0)
		return -1;

	ph_end = eh->phoff + (uint)eh->phnum * sizeof(struct proghdr);
	if (eh->phoff >= size || ph_end > size || ph_end < eh->phoff)
		return -1;

	brk = USERBASE;
	ph = (const struct proghdr *)(blob + eh->phoff);
	for (i = 0; i < eh->phnum; i++, ph++) {
		uint seg_end;

		if (ph->type != ELF_PROG_LOAD)
			continue;
		if (ph->memsz == 0)
			continue;
		if (ph->filesz > ph->memsz)
			return -1;
		if (ph->off > size || ph->filesz > size - ph->off)
			return -1;
		if (ph->vaddr < USERBASE)
			return -1;
		seg_end = ph->vaddr + ph->memsz;
		if (seg_end < ph->vaddr || seg_end > USEREND)
			return -1;

		perm = PTE_P;
		if (ph->flags & ELF_PROG_FLAG_WRITE)
			perm |= PTE_W;

		if (loaduvm_seg(pgdir, ph->vaddr,
				ph->filesz ? blob + ph->off : 0, ph->filesz,
				ph->memsz, perm) < 0)
			return -1;
		if (seg_end > brk)
			brk = seg_end;
	}

	if (eh->entry < USERBASE || eh->entry >= USEREND)
		return -1;
	*entry = eh->entry;
	if (out_brk)
		*out_brk = brk;
	return 0;
}

/*
 * 在已映射的用户栈页上构造 Linux 风格初始栈。
 * argv/envp 为内核侧以 NULL 结尾的字符串表；argv 空则用 name 作 argv[0]。
 * 成功时 *out_esp 指向 argc，且 16 字节对齐。
 */
static int exec_stack_argv(pagetable_t pgdir, const char *name,
			   char *const *argv, char *const *envp, uint *out_esp)
{
	const char *kargv[MAXARG + 1];
	const char *kenvp[MAXENV + 1];
	uint uargv[MAXARG + 1];
	uint uenvp[MAXENV + 1];
	uint sp, len, tab, argc_u, aux[2];
	int argc, envc, i;

	if (!pgdir || !out_esp || !name)
		return -1;

	argc = 0;
	if (argv) {
		for (i = 0; argv[i]; i++) {
			if (argc >= MAXARG)
				return -1;
			kargv[argc++] = argv[i];
		}
	}
	if (argc == 0) {
		kargv[0] = name;
		argc = 1;
	}
	kargv[argc] = 0;

	envc = 0;
	if (envp) {
		for (i = 0; envp[i]; i++) {
			if (envc >= MAXENV)
				return -1;
			kenvp[envc++] = envp[i];
		}
	}
	kenvp[envc] = 0;

	/* 1) 信息块：argv / envp 字符串（高址 → 低址） */
	sp = USERSTACK;
	for (i = 0; i < argc; i++) {
		len = strlen_u(kargv[i]) + 1;
		if (len > NNAME || sp < USERSTACK - PGSIZE + len)
			return -1;
		sp -= len;
		if (copyout(pgdir, sp, kargv[i], len) < 0)
			return -1;
		uargv[i] = sp;
	}
	uargv[argc] = 0;

	for (i = 0; i < envc; i++) {
		len = strlen_u(kenvp[i]) + 1;
		if (len > NNAME || sp < USERSTACK - PGSIZE + len)
			return -1;
		sp -= len;
		if (copyout(pgdir, sp, kenvp[i], len) < 0)
			return -1;
		uenvp[i] = sp;
	}
	uenvp[envc] = 0;

	/*
	 * 2) 指针表 + argc + AT_NULL：
	 *    单词数 = 1(argc) + (argc+1) + (envc+1) + 2(aux)
	 *    最终 %esp 须 16 字节对齐（SysV i386 ABI）。
	 *
	 * TODO: 待消化
	 */
	tab = (1 + (uint)(argc + 1) + (uint)(envc + 1) + 2) * sizeof(uint);
	if (sp < USERSTACK - PGSIZE + tab)
		return -1;
	sp = (sp - tab) & ~0xf;
	if (sp < USERSTACK - PGSIZE)
		return -1;

	argc_u = (uint)argc;
	if (copyout(pgdir, sp, &argc_u, sizeof(uint)) < 0)
		return -1;
	if (copyout(pgdir, sp + sizeof(uint), uargv,
		    (uint)(argc + 1) * sizeof(uint)) < 0)
		return -1;
	if (copyout(pgdir,
		    sp + sizeof(uint) + (uint)(argc + 1) * sizeof(uint),
		    uenvp, (uint)(envc + 1) * sizeof(uint)) < 0)
		return -1;

	aux[0] = AT_NULL;
	aux[1] = 0;
	if (copyout(pgdir,
		    sp + sizeof(uint) +
			    (uint)(argc + 1 + envc + 1) * sizeof(uint),
		    aux, sizeof(aux)) < 0)
		return -1;

	*out_esp = sp;
	return 0;
}

/*
 * 在新页表加载 blob + 用户栈；成功时切换 p->pagetable 并更新 trapframe。
 * 失败时释放 newpg，旧映像保持不变。
 */
int exec_load(struct proc *p, struct trapframe *tf, const void *blob, uint size,
	      const char *name, char *const *argv, char *const *envp)
{
	pagetable_t oldpg, newpg;
	void *ustack;
	uint entry, esp, heap_end;

	if (!p || !tf || !blob || size == 0 || size > EXEC_MAX_FILE || !name)
		return -1;

	newpg = uvmcreate();
	if (newpg == 0)
		return -1;

	heap_end = USERBASE;
	if (is_elf(blob, size)) {
		if (exec_load_elf(newpg, blob, size, &entry, &heap_end) < 0)
			goto bad;
	} else {
		if (size > USEREND - USERBASE)
			goto bad;
		if (loaduvm(newpg, USERBASE, blob, size) < 0)
			goto bad;
		entry = USERBASE;
		heap_end = USERBASE + size;
	}

	ustack = alloc_page();
	if (ustack == 0)
		goto bad;
	if (uvmmap(newpg, USERSTACK - PGSIZE, (uint)ustack, PGSIZE,
		   PTE_W | PTE_P) < 0) {
		free_page(ustack);
		goto bad;
	}

	if (exec_stack_argv(newpg, name, argv, envp, &esp) < 0)
		goto bad;

	oldpg = p->pagetable;
	p->pagetable = newpg;
	p->sz = USERSTACK;
	/* 程序断点：数据段末；堆向 USERHEAP_TOP 增长 */
	if (heap_end < USERBASE)
		heap_end = USERBASE;
	if (heap_end > USERHEAP_TOP)
		heap_end = USERHEAP_TOP;
	p->brk = heap_end;
	p->brk_start = heap_end;
	vma_clear(p);
	fpu_clear(p);
	proc_name_from_path(p, name);
	signal_exec_reset(p);

	tf->eip = entry;
	tf->esp = esp;
	tf->cs = SEG_UCODE | DPL_USER;
	tf->ss = SEG_UDATA | DPL_USER;
	tf->ds = SEG_UDATA | DPL_USER;
	tf->eflags = 0x202;

	current_user_pgdir = newpg;
	uvmfree(oldpg);

	printk(KERN_DEBUG "execve: pid=%d name=%s eip=0x%x esp=0x%x\n",
	       p->pid, p->name, tf->eip, tf->esp);
	return 0;

bad:
	uvmfree(newpg);
	return -1;
}

/* 从 VFS 读入普通文件到内核缓冲；成功返回缓冲（调用方 kfree），*out_size 为长度 */
static char *exec_read_file(const char *path, uint *out_size)
{
	struct inode *ip;
	char *buf;
	uint size;
	int n;

	ip = fs_namei(path);
	if (!ip)
		return 0;
	if (ip->type != T_FILE || ip->size == 0 || ip->size > EXEC_MAX_FILE) {
		fs_iput(ip);
		return 0;
	}

	size = ip->size;
	buf = kmalloc(size);
	if (!buf) {
		fs_iput(ip);
		return 0;
	}
	n = fs_readi(ip, buf, 0, size);
	fs_iput(ip);
	if (n < 0 || (uint)n != size) {
		kfree(buf);
		return 0;
	}
	*out_size = size;
	return buf;
}

int execve(struct proc *p, struct trapframe *tf, const char *path,
	   char *const *argv, char *const *envp)
{
	const struct userbin *bin;
	char *buf;
	uint size;
	int r;

	if (!p || !tf || !path || !path[0])
		return -1;

	/* 1) 文件系统路径 */
	buf = exec_read_file(path, &size);
	if (buf) {
		r = exec_load(p, tf, buf, size, path, argv, envp);
		kfree(buf);
		return r;
	}

	/* 2) 嵌入二进制（init / sh） */
	bin = userbin_lookup(path);
	if (bin)
		return exec_load(p, tf, bin->blob, bin->size, bin->name, argv,
				 envp);

	printk(KERN_ERR "execve: pid=%d cannot find '%s'\n", p->pid, path);
	return -1;
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

	if (execve(p, tf, path, 0, 0) < 0)
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
