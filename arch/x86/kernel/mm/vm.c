/**
 * x86 32 位两级页表里，PDE 和 PTE 格式相同，都是 32 位（4 字节）。
 * 位编号：31 为最高位，0 为最低位（与 Intel SDM 一致）。
 *  31                    12 11  9 8 7 6 5 4 3 2 1 0
 * ┌────────────────────────┬─────┬─┬─┬─┬─┬─┬─┬─┬─┬─┐
 * │   Physical Base        │ AVL │G│P│D│A│P│P│U│R│P│
 * │   (PPN, 20 bits)       │ 3b  │ │S│ │ │C│W│/│/│ │
 * │                        │     │ │ │ │ │D│T│S│w│ │
 * └────────────────────────┴─────┴─┴─┴─┴─┴─┴─┴─┴─┴─┘
 */

#include "../types.h"
#include "../defs.h"
#include "memlayout.h"
#include "mmu.h"
#include "../x86.h"
#include "../ipc/shm.h"

/* 内核页目录, 所有内核线程共享。和 Linux 一样：内核页表是全局共用的 */
pagetable_t kernel_pgdir;
uint kernel_cr3;

static inline uint pgdir_phys(pagetable_t pgdir)
{
	return pgdir ? V2P((uint)pgdir) : 0;
}

static inline pagetable_t pde_pt_kva(pde_t pde)
{
	return (pagetable_t)P2V(PTE_ADDR(pde));
}

static inline void *pte_page_kva(pte_t pte)
{
	return (void *)P2V(PTE_ADDR(pte));
}

/* -------------------------------------------------------------------------- */
/* 页表遍历与映射（内核 / 用户页表共用）                                         */
/* -------------------------------------------------------------------------- */

/*
 * Return the address of the PTE for virtual address va.
 * Create intermediate page-table pages when alloc != 0.
 */
static pte_t *walk(pagetable_t pgdir, uint va, int alloc, int user)
{
	pde_t *pde = &pgdir[PDX(va)];
	pagetable_t pt;

	if (*pde & PTE_P) {
		pt = pde_pt_kva(*pde);
	} else {
		if (!alloc)
			return 0;
		pt = (pagetable_t)alloc_page();
		if (pt == 0)
			return 0;
		/*
		 * 用户页表中的 PDE 须带 PTE_U，否则 ring3 访问该页表下任何页
		 * 都会 #PF（err 常见为 0x5 保护违规）。
		 */
		if (user)
			*pde = PA2PTE(V2P((uint)pt)) | PTE_P | PTE_W | PTE_U;
		else
			*pde = PA2PTE(V2P((uint)pt)) | PTE_P | PTE_W;
	}
	return &pt[PTX(va)];
}

static int mappages(pagetable_t pgdir, uint va, uint size, uint pa, int perm,
		    int user)
{
	pte_t *pte;
	uint a, last;

	if ((va % PGSIZE) != 0 || (pa % PGSIZE) != 0) {
		printf("page not aligned: va=%p, pa=%p, size=%p\n",
			va, pa, size);
		return -1;
	}

	if (size == 0 || (size % PGSIZE) != 0)
	{
		printf("size not aligned: va=%p, pa=%p, size=%p\n",
			va, pa, size);
		return -1;
	}

	a = va;
	last = va + size - PGSIZE;
	for (;;) {
		pte = walk(pgdir, a, 1, user);
		if (pte == 0) {
			printf("walk failed: va=%p, a=%p\n", va, a);
			return -1;
		}
		if (*pte & PTE_P) {
			printf("pte already mapped: va=%p, a=%p, pte=%p\n", va, a, pte);
			return -1;
		}
		*pte = PA2PTE(pa) | perm | PTE_P;
		if (a == last)
			break;
		a += PGSIZE;
		pa += PGSIZE;
	}
	return 0;
}

/*
 * 解除 va 起连续 npages 页的映射；do_free 非 0 时一并释放物理页。
 * allow_missing：按需分页空洞（从未触碰）可跳过；内核映射须 allow_missing=0。
 */
static int unmappages(pagetable_t pgdir, uint va, uint npages, int do_free,
		      int allow_missing)
{
	pte_t *pte;
	uint a;

	if (pgdir == 0 || (va % PGSIZE) != 0 || npages == 0)
		return -1;

	for (a = va; a < va + npages * PGSIZE; a += PGSIZE) {
		pte = walk(pgdir, a, 0, 0);
		if (pte == 0 || !(*pte & PTE_P)) {
			if (allow_missing)
				continue;
			return -1;
		}
		if (do_free)
			put_page(pte_page_kva(*pte));
		*pte = 0;
	}
	return 0;
}

/* 仅当 pgdir 为当前 CR3 时刷新 TLB（用户页表卸载场景） */
static void tlb_flush_if_active(pagetable_t pgdir)
{
	if (r_cr3() == pgdir_phys(pgdir))
		w_cr3(pgdir_phys(pgdir));
}

/* -------------------------------------------------------------------------- */
/* 内核地址空间                                                                */
/* -------------------------------------------------------------------------- */

void kvmmap(uint va, uint pa, uint size, int perm)
{
	if (mappages(kernel_pgdir, va, size, pa, perm, 0) < 0)
		panic("kvmmap");
}

void kvmunmap(uint va, uint size)
{
	// 内核恒等映射 [0, physmem_top) 时，很多物理页并不是“为这个映射专门分配的”
	// ——可能是 boot 区、MMIO、buddy 池里的页。kvmunmap 只拆映射，不能随便 free_page
	if ((va % PGSIZE) != 0 || size == 0 || (size % PGSIZE) != 0)
		panic("kvmunmap");
	if (unmappages(kernel_pgdir, va, size / PGSIZE, 0, 0) < 0)
		panic("kvmunmap");

	/** 刷新 TLB */
	w_cr3(kernel_cr3);
}

/* 不保证物理连续，可能返回物理上不连续的内存 */
void *kvmalloc(uint va, int perm)
{
	(void)va;
	(void)perm;
	return 0;
}

/*
 * Build a two-level page table (page directory + page tables)
 * with a 1:1 map of [0, physmem_top).
 */
static void kvminit_map(void)
{
	// 映射范围为：1 * 1024(PDE) * 1024(PTE) * 4096(PAGE_SIZE) = 4GB
	static pde_t boot_pgdir[MAX_KERNEL_PT] __attribute__((aligned(PGSIZE)));

	kernel_pgdir = boot_pgdir;
	memset(kernel_pgdir, 0, PGSIZE);

	kvmmap(0, 0, KERNEL_BOOT_IDMAP_END, PTE_W);
	kvmmap(KERNBASE, 0, physmem_top, PTE_W);
	kernel_cr3 = pgdir_phys(kernel_pgdir);
}

/**
 * 内存页有没有映射和内存有没有分配是两回事：
 * - alloc_page 分配的是物理页；
 * - mappages 映射的是虚拟页；
 */
void kvm_init(void)
{
	kvminit_map();

	/* CPU 在写入 CR3 时，会自动刷新整个 TLB，使旧的虚拟到物理的映射失效 */
	w_cr3(kernel_cr3);
	w_cr0(r_cr0() | CR0_PG);
	printk(KERN_INFO "vm: operating system paging enabled, kernel pgdir: %p\n", kernel_pgdir);

	// for (int i = 0; i < MAX_KERNEL_PT; i++) {
	// 	if (kernel_pgdir[i] != 0)
	// 		printf("  kpage dir[%d]: %p\n", i, kernel_pgdir[i]);
	// }
}

/* -------------------------------------------------------------------------- */
/* 用户地址空间                                                                */
/* -------------------------------------------------------------------------- */

struct mm_struct *mm_create(void)
{
	struct mm_struct *mm;

	mm = kcalloc(1, sizeof(*mm));
	if (!mm)
		return 0;
	initlock(&mm->lock, "mm");
	mm->user_base = USERLOAD;
	mm->task_size = USEREND;
	mm->mmap_base = USERHEAP_TOP;
	mm->stack_top = USERSTACK;
	mm->brk = USERLOAD;
	mm->brk_start = USERLOAD;
	mm->pgdir = uvmcreate();
	if (!mm->pgdir) {
		kfree(mm);
		return 0;
	}
	return mm;
}

void mm_destroy(struct mm_struct *mm)
{
	if (!mm)
		return;
	if (mm->pgdir)
		uvmfree(mm->pgdir);
	kfree(mm);
}

pagetable_t uvmcreate(void)
{
	pagetable_t pgdir;
	int i;

	pgdir = (pagetable_t)alloc_page();
	if (pgdir == 0)
		return 0;
	memset(pgdir, 0, PGSIZE);

	/*
	 * 继承内核页目录项，使 trap 时 ISR 仍可执行
	 */
	for (i = PDX(KERNBASE); i < MAX_KERNEL_PT; i++) {
		if (kernel_pgdir[i] & PTE_P)
			pgdir[i] = kernel_pgdir[i];
	}
	return pgdir;
}

/*
 * 将 kernel_pgdir 的监督者映射复制到 pgdir（跳过用户 VA 窗口）。
 * 供 fork 等场景复用。
 */
void uvmcopy_kernel(pagetable_t pgdir)
{
	int i;

	if (pgdir == 0)
		return;
	for (i = PDX(KERNBASE); i < MAX_KERNEL_PT; i++) {
		if (kernel_pgdir[i] & PTE_P)
			pgdir[i] = kernel_pgdir[i];
	}
}

/*
 * fork：Copy-on-Write（对齐 Linux do_cow_fault / xv6 COW 教学模型）。
 * - 共享父进程物理页（get_page），不立即拷贝
 * - 原可写页：父子均清 PTE_W、置 PTE_COW，写时缺页再复制
 * - 本就只读页：仅共享，不标 COW（写入则为真保护错）
 */
int uvmcopy(pagetable_t old, pagetable_t new, uint sz)
{
	pagetable_t oldpt;
	pte_t *pte;
	uint i, j, va, pa;
	int perm;

	if (old == 0 || new == 0 || sz == 0)
		return -1;
	if (sz > USEREND)
		sz = USEREND;
	if (sz <= USERBASE)
		return 0;

	for (i = PDX(USERBASE); i <= PDX(sz - 1); i++) {
		if (!(old[i] & PTE_P) || !(old[i] & PTE_U))
			continue;
		oldpt = pde_pt_kva(old[i]);
		for (j = 0; j < 1024; j++) {
			va = (i << 22) | (j << 12);
			if (va >= sz)
				break;
			pte = &oldpt[j];
			if (!(*pte & PTE_P))
				continue;
			pa = PTE_ADDR(*pte);
			get_page((void *)P2V(pa));

			if ((*pte & PTE_W) || (*pte & PTE_COW)) {
				/* 可写或已 COW：双方只读 + COW */
				*pte = (*pte | PTE_COW) & ~PTE_W;
				perm = PTE_U | PTE_P | PTE_COW;
			} else {
				/* 只读代码页等：共享且保持只读 */
				perm = PTE_U | PTE_P;
			}
			if (uvmmap(new, va, pa, PGSIZE, perm) < 0) {
				put_page((void *)P2V(pa));
				return -1;
			}
		}
	}
	tlb_flush_if_active(old);
	return 0;
}

/*
 * 处理写 COW 页：sole owner 直接恢复可写；否则分配新页拷贝。
 * 成功返回 0，调用方重试故障指令。
 */
int uvm_cow_fault(pagetable_t pgdir, uint va)
{
	pte_t *pte;
	uint pa;
	char *mem;
	uint i;

	va = PGROUNDDOWN(va);
	if (pgdir == 0 || va < USERBASE || va >= USEREND)
		return -1;

	pte = walk(pgdir, va, 0, 0);
	if (pte == 0 || !(*pte & PTE_P) || !(*pte & PTE_COW))
		return -1;

	pa = PTE_ADDR(*pte);
	if (page_refcount((void *)P2V(pa)) == 1) {
		*pte = (*pte | PTE_W) & ~PTE_COW;
		tlb_flush_if_active(pgdir);
		return 0;
	}

	mem = alloc_page();
	if (mem == 0)
		return -1;
	for (i = 0; i < PGSIZE; i++)
		mem[i] = ((char *)P2V(pa))[i];

	put_page((void *)P2V(pa));
	*pte = PA2PTE(V2P((uint)mem)) | PTE_P | PTE_U | PTE_W;
	tlb_flush_if_active(pgdir);
	return 0;
}

/* 将用户空间从 oldsz 收缩到 newsz；成功返回 newsz */
uint uvmdealloc(pagetable_t pgdir, uint oldsz, uint newsz)
{
	if (pgdir == 0)
		return 0;
	if (newsz >= oldsz)
		return oldsz;

	if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
		uint npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;

		if (uvmunmap(pgdir, PGROUNDUP(newsz), npages, 1) < 0)
			return 0;
	}
	return newsz;
}

/*
 * 将用户堆断点从 oldsz 增长到 newsz（按需分页：不立即分配物理页）。
 * 物理页在用户首次访问或 copyin/copyout 时由 uvm_demand_fault 填零。
 * 成功返回 newsz，失败返回 0。
 */
uint uvmalloc(pagetable_t pgdir, uint oldsz, uint newsz)
{
	if (pgdir == 0 || newsz < oldsz)
		return 0;
	if (newsz > USERHEAP_TOP)
		return 0;
	return newsz;
}

/*
 * 按需填零：VA 落在堆 [brk_start, brk) 或匿名 VMA 内时分配清零页并映射。
 * write 非 0 表示写访问（只读 VMA 拒绝）。成功返回 0。
 */
int uvm_demand_fault(struct proc *p, uint va, int write)
{
	struct vma *v;
	char *mem;
	int perm;
	uint page;
	pagetable_t pgdir;

	pgdir = proc_pagetable(p);
	if (!p || !pgdir)
		return -1;

	page = PGROUNDDOWN(va);
	if (page < proc_user_base(p) || page >= USEREND)
		return -1;

	/* 已映射则无需再填（竞态下幂等） */
	if (walkaddr(pgdir, page) != 0)
		return 0;

	/* 堆：页与 [brk_start, brk) 相交 */
	if (page < proc_brk(p) && page + PGSIZE > proc_brk_start(p)) {
		perm = PTE_P | PTE_W;
	} else if ((v = vma_lookup(p, page)) != 0) {
		if (vma_shmid(v) >= 0)
			return shm_demand_fault(p, v, page, write);
		if (!(v->prot & (PROT_READ | PROT_WRITE | PROT_EXEC)))
			return -1;
		if (write && !(v->prot & PROT_WRITE))
			return -1;
		perm = PTE_P;
		if (v->prot & PROT_WRITE)
			perm |= PTE_W;
	} else {
		return -1;
	}

	mem = alloc_page();
	if (mem == 0)
		return -1;
	memset(mem, 0, PGSIZE);
	if (uvmmap(pgdir, page, V2P((uint)mem), PGSIZE, perm) < 0) {
		free_page(mem);
		return -1;
	}
	return 0;
}

/* 内核路径访问用户页前：缺页则按需填零 */
static int ensure_user_page(pagetable_t pgdir, uint va, int write)
{
	struct proc *p;
	pte_t *pte;

	va = PGROUNDDOWN(va);
	pte = walk(pgdir, va, 0, 0);
	if (pte && (*pte & PTE_P)) {
		if (write && (*pte & PTE_COW)) {
			if (uvm_cow_fault(pgdir, va) < 0)
				return -1;
		}
		return 0;
	}

	p = myproc();
	if (!p || proc_pagetable(p) != pgdir)
		return -1;
	return uvm_demand_fault(p, va, write);
}

int uvmmap(pagetable_t pgdir, uint va, uint pa, uint size, int perm)
{
	if (pgdir == 0)
		return -1;
	return mappages(pgdir, va, size, pa, perm | PTE_U, 1);
}

int uvmunmap(pagetable_t pgdir, uint va, uint npages, int do_free)
{
	/* 允许按需分页空洞（从未触碰的页） */
	if (unmappages(pgdir, va, npages, do_free, 1) < 0)
		return -1;
	/* 进程页表未必是当前 CR3，按需刷新 */
	tlb_flush_if_active(pgdir);
	return 0;
}

/*
 * 将 src（长度 sz）拷贝到新分配物理页，并以只读用户页映射到 va。
 * sz 须小于一页；失败时释放已分配物理页。
 */
int uvminit(pagetable_t pgdir, uint va, const void *src, uint sz)
{
	char *mem;
	uint i;

	if (pgdir == 0 || src == 0 || sz >= PGSIZE)
		return -1;

	mem = alloc_page();
	if (mem == 0)
		return -1;
	memset(mem, 0, PGSIZE);
	for (i = 0; i < sz; i++)
		mem[i] = ((const char *)src)[i];
	if (uvmmap(pgdir, va, V2P((uint)mem), PGSIZE, PTE_P) < 0) {
		free_page(mem);
		return -1;
	}
	return 0;
}

/*
 * 返回 va 对应用户页的内核可访问地址（含页内偏移）；须 PTE_P 且 PTE_U。
 */
uint walkaddr(pagetable_t pgdir, uint va)
{
	pte_t *pte;

	if (pgdir == 0)
		return 0;
	pte = walk(pgdir, va, 0, 0);
	if (pte == 0 || !(*pte & PTE_P) || !(*pte & PTE_U))
		return 0;
	return P2V(PTE_ADDR(*pte)) | (va & (PGSIZE - 1));
}

/*
 * 从用户页表 copyin 至多 n 字节到内核缓冲区；跨页自动拆分。
 */
int copyin(pagetable_t pgdir, void *dst, uint srcva, uint n)
{
	char *d = dst;
	uint va, pa, chunk;

	if (pgdir == 0 || dst == 0)
		return -1;
	while (n > 0) {
		uint i;

		va = PGROUNDDOWN(srcva);
		if (ensure_user_page(pgdir, va, 0) < 0)
			return -1;
		pa = walkaddr(pgdir, va);
		if (pa == 0)
			return -1;
		chunk = PGSIZE - (srcva - va);
		if (chunk > n)
			chunk = n;
		pa += srcva - va;
		for (i = 0; i < chunk; i++)
			d[i] = ((char *)pa)[i];
		d += chunk;
		srcva += chunk;
		n -= chunk;
	}
	return 0;
}

/*
 * 从内核缓冲区 copyout 至多 n 字节到用户页表；跨页自动拆分。
 * 写前按需填零 / break COW（内核写不经 #PF）。
 */
int copyout(pagetable_t pgdir, uint dstva, const void *src, uint n)
{
	const char *s = src;
	uint va, pa, chunk;

	if (pgdir == 0 || src == 0)
		return -1;
	while (n > 0) {
		uint i;

		va = PGROUNDDOWN(dstva);
		if (ensure_user_page(pgdir, va, 1) < 0)
			return -1;
		pa = walkaddr(pgdir, va);
		if (pa == 0)
			return -1;
		chunk = PGSIZE - (dstva - va);
		if (chunk > n)
			chunk = n;
		pa += dstva - va;
		for (i = 0; i < chunk; i++)
			((char *)pa)[i] = s[i];
		s += chunk;
		dstva += chunk;
		n -= chunk;
	}
	return 0;
}

/*
 * 将 src（长度 sz）按页拷贝到 va 起连续用户页。
 * 权限：用户可读写（flat binary 含 .data/.bss，须可写；教学实现不区分 RX/RW）。
 * va 须页对齐；va+sz 不得超过 USEREND。
 */
int loaduvm(pagetable_t pgdir, uint va, const void *src, uint sz)
{
	char *mem;
	uint a, off, n;

	if (pgdir == 0 || src == 0 || sz == 0 || (va % PGSIZE) != 0)
		return -1;
	if (va < USERLOAD || va + sz > USEREND)
		return -1;

	for (off = 0; off < sz; off += PGSIZE) {
		mem = alloc_page();
		if (mem == 0)
			return -1;
		memset(mem, 0, PGSIZE);
		n = sz - off;
		if (n > PGSIZE)
			n = PGSIZE;
		for (a = 0; a < n; a++)
			mem[a] = ((const char *)src + off)[a];
		a = va + off;
		if (uvmmap(pgdir, a, V2P((uint)mem), PGSIZE, PTE_P | PTE_W) < 0) {
			free_page(mem);
			return -1;
		}
	}
	return 0;
}

/*
 * 将 src[0..filesz) 装入用户空间 [va, va+filesz)，并保证 [va, va+memsz)
 * 已映射（多出部分为 0，供 .bss）。va 不必页对齐；页按需分配。
 * perm 传给 uvmmap（自动加 PTE_U）。
 */
int loaduvm_seg(pagetable_t pgdir, uint va, const void *src, uint filesz,
		uint memsz, int perm)
{
	uint i, end, file_end, pa;
	char *mem;
	const char *s = src;

	if (pgdir == 0 || (filesz > 0 && src == 0) || memsz < filesz)
		return -1;
	if (memsz == 0)
		return 0;
	if (va < USERLOAD || va + memsz > USEREND)
		return -1;

	end = va + memsz;
	file_end = va + filesz;

	for (i = PGROUNDDOWN(va); i < end; i += PGSIZE) {
		uint c_lo, c_hi, n;

		pa = walkaddr(pgdir, i);
		if (pa == 0) {
			mem = alloc_page();
			if (mem == 0)
				return -1;
			memset(mem, 0, PGSIZE);
			if (uvmmap(pgdir, i, V2P((uint)mem), PGSIZE, perm) < 0) {
				free_page(mem);
				return -1;
			}
		} else {
			mem = (char *)PGROUNDDOWN(pa);
		}

		/* 本页与文件映像 [va, file_end) 的交集 */
		c_lo = va > i ? va : i;	/* 起始地址 */
		c_hi = file_end < i + PGSIZE ? file_end : i + PGSIZE;	/* 结束地址 */
		if (c_lo < c_hi) {
			n = c_hi - c_lo;	/* 拷贝大小 */
			memcpy(mem + (c_lo - i), s + (c_lo - va), n);
		}
	}
	return 0;
}

/*
 * 释放 pgdir 中 [USERBASE, USEREND) 的用户映射及用户专用页表页；
 * 继承自 kernel_pgdir 的 PDE（无 PTE_U）保留不动，最后释放页目录。
 */
void uvmfree(pagetable_t pgdir)
{
	pte_t *pte;
	int i;

	if (pgdir == 0)
		return;

	for (i = PDX(USERBASE); i <= PDX(USEREND - 1); i++) {
		if (pgdir[i] & PTE_P && (pgdir[i] & PTE_U)) {
			pagetable_t pt = pde_pt_kva(pgdir[i]);
			int j;

			for (j = 0; j < 1024; j++) {
				pte = &pt[j];
				if (!(*pte & PTE_P))
					continue;
				put_page(pte_page_kva(*pte));
				*pte = 0;
			}
			free_page(pt);
			pgdir[i] = 0;
		}
	}
	free_page(pgdir);
}

/*
 * 从用户空间拷贝以 '\0' 结尾的字符串；max 含结尾 NUL 上限。
 */
int copyinstr(pagetable_t pgdir, char *dst, uint srcva, uint max)
{
	uint i;

	if (pgdir == 0 || dst == 0 || max == 0)
		return -1;
	for (i = 0; i + 1 < max; i++) {
		if (copyin(pgdir, dst + i, srcva + i, 1) < 0)
			return -1;
		if (dst[i] == '\0')
			return 0;
	}
	return -1;
}
