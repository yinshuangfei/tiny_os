/*
 * 伙伴系统（buddy allocator）物理页分配器
 *
 * 思路与 Linux mm/page_alloc.c 相同：把物理内存按 2 的幂次划分成块，
 * 每个 order 维护一条空闲块链表；分配时分裂大块，释放时尝试与 buddy 合并。
 *
 * order 与块大小的关系：
 *   order 0 →  1 页 =  4 KiB
 *   order 1 →  2 页 =  8 KiB
 *   order 2 →  4 页 = 16 KiB
 *   ...
 *   order n →  2^n 页
 *
 * 例：释放 8 页后，相邻 buddy 可合并成 order=3 的 16 页块。

 * (1)系统启动时，会通过 BIOS 中断（如 INT 0x15, eax=0xe820）探测物理内存布局，生成
 * 内存映射表。
 * (2)内核在初始化内存管理子系统时，会将已分配给内核自身核心数据结构（如内核代码段、
 * 数据段、内核栈等）的内存从可用内存池中扣除。
 * (3)随后，内核才会将剩余的物理内存页交给伙伴系统（Buddy System）等通用分配器进行管理。
 */
#include "../types.h"
#include "../defs.h"
#include "memlayout.h"
#include "../list.h"
#include "../lock/spinlock.h"
#include "../utils.h"

extern char end[];	/* kernel.ld: .bss 结束后的第一个地址 */

#define PAGE_INUSE  ((unsigned int)-1)	/* 已分配页标记，不在任何 free_area 链上 */

/*
 * 每个物理页一条元数据，类似 Linux 的 struct page / mem_map。
 * 空闲时挂在 free_area[order] 上；分配后 order = PAGE_INUSE。
 * _refcount：order-0 用户页 COW 共享（类 Linux page->_refcount）。
 */
struct page {
	struct list_head list;
	unsigned int order;
	unsigned int _refcount;
};

/*
 * 每个 order 对应一类空闲块：
 * free_list 上每个节点是一块 2^order 页连续物理内存的首页。
 */
struct free_area {
	struct list_head free_list;
	unsigned int nr_free;	/* 该 order 上空闲块个数（不是页数） */
};

static struct free_area free_area[MAX_ORDER];
static struct spinlock pmm_lock;
static struct page *mem_map;	/* 紧贴内核 end[]，按可分配区页号索引 */
static uint mem_start;		/* 可分配区起始物理地址（页对齐） */
static uint mem_end;		/* 可分配区结束物理地址（= physmem_top） */
static unsigned int nr_pages;	/* 可分配页总数 */
static unsigned int nr_free;	/* 当前空闲页总数（所有 order 之和） */

void *memset(void *dst, int c, uint n)
{
	uchar *p = dst;

	while (n-- > 0)
		*p++ = c;
	return dst;
}

void *memcpy(void *dst, const void *src, uint n)
{
	uchar *d = dst;
	const uchar *s = src;

	while (n-- > 0)
		*d++ = *s++;
	return dst;
}

/* 物理地址 → mem_map 中的 struct page */
static struct page *addr_to_page(void *addr)
{
	uint a = (uint)addr;

	if (a < mem_start || a >= mem_end || (a & (PGSIZE - 1)) != 0)
		return 0;
	return &mem_map[(a - mem_start) / PGSIZE];
}

/* struct page → 该页首字节物理地址 */
static void *page_to_addr(struct page *page)
{
	return (void *)(mem_start + (page - mem_map) * PGSIZE);
}

/*
 * 求 buddy 页：同一 order 下，块内页号相差 2^order 的那一块。
 * 公式 idx ^ (1 << order)，例如 order=1 时页 2 与页 3 互为 buddy。
 */
static struct page *page_buddy(struct page *page, unsigned int order)
{
	unsigned long idx = page - mem_map;
	// 快速找到“能和我拼成更大块”的唯一邻居
	unsigned long buddy = idx ^ (1UL << order);

	if (buddy >= nr_pages)
		return 0;
	return &mem_map[buddy];
}

static void free_area_add(struct page *page, unsigned int order)
{
	page->order = order;
	list_add(&page->list, &free_area[order].free_list);
	free_area[order].nr_free++;
	nr_free += 1U << order;
}

static void free_area_del(struct page *page, unsigned int order)
{
	list_del(&page->list);
	free_area[order].nr_free--;
	nr_free -= 1U << order;
}

/*
 * 释放一页（或一块 2^order 页）：先挂到对应 order 链表，
 * 若 buddy 也是同 order 的空闲块，则合并成 order+1，反复直到无法合并。
 */
static void __free_one_page(struct page *page, unsigned int order)
{
	unsigned long idx;
	struct page *buddy;

	idx = page - mem_map;
	while (order < MAX_ORDER - 1) {
		buddy = page_buddy(&mem_map[idx], order);
		if (!buddy || buddy->order != order)
			break;
		free_area_del(buddy, order);
		idx &= ~(1UL << order);	/* 合并后块首地址取两者中较小的页号 */
		order++;
	}
	free_area_add(&mem_map[idx], order);
}

/*
 * 分配 2^order 页：从 free_area[order] 取块；若没有则从更大 order 取块并分裂。
 *
 * 分裂示例（要 order=1 的 2 页，只有 order=3 的 8 页块）：
 *   8 页块 → 拆出 4 页 buddy 挂到 order=2
 *          → 再拆出 2 页 buddy 挂到 order=1
 *          → 返回剩余 2 页给调用者
 */
static struct page *__alloc_pages(unsigned int order)
{
	unsigned int o;
	struct page *page;
	struct page *buddy;

	if (order >= MAX_ORDER)
		return 0;

	for (o = order; o < MAX_ORDER; o++) {
		if (list_empty(&free_area[o].free_list))
			continue;

		page = list_first_entry(&free_area[o].free_list, struct page, list);
		free_area_del(page, o);

		while (o > order) {
			o--;
			buddy = page + (1UL << o);	/* 分裂出的后半段 */
			free_area_add(buddy, o);
		}

		page->order = PAGE_INUSE;
		page->_refcount = 1;	/* 调用方持有一份引用 */
		return page;
	}
	return 0;
}

/*
 * pmm: physical memory manager
 *
 * mem_map 放在内核 end[] 之后（随 RAM 大小动态伸缩），其后为 buddy 池：
 *   [end, mem_start)     — struct page[] 元数据（不入池）
 *   [mem_start, physmem_top) — 可分配物理页
 *
 * 释放时按最大对齐块入池，减少启动阶段反复合并。
 * 须在 kvm_init 之前调用（此时尚未开分页，物理地址 == 线性地址）。
 */
void pmm_init(void)
{
	unsigned int i, order;
	uint base, map_bytes, addr, left, idx;
	char total_size[HUMAN_SIZE_MAX] = {0};

	base = PGROUNDUP((uint)end);
	if (base >= physmem_top)
		panic("pmm: no memory after kernel");

	/*
	 * 按「[base, physmem_top) 每页一条 page」高估 mem_map。
	 * 元数据本身占用其中一部分页，故实际 nr_pages 略小；多留的几页条目可忽略。
	 * （精确收敛会在 map 缩小/放大之间振荡，故不迭代。）
	 */
	map_bytes = PGROUNDUP(((physmem_top - base) / PGSIZE) *
			      (uint)sizeof(struct page));
	if (base + map_bytes >= physmem_top)
		panic("pmm: mem_map does not fit");

	mem_map = (struct page *)base;
	mem_start = base + map_bytes;
	mem_end = physmem_top;
	nr_pages = (mem_end - mem_start) / PGSIZE;
	if (nr_pages == 0)
		panic("pmm: zero pages");
	nr_free = 0;
	initlock(&pmm_lock, "pmm");

	memset(mem_map, 0, map_bytes);
	for (i = 0; i < MAX_ORDER; i++) {
		INIT_LIST_HEAD(&free_area[i].free_list);
		free_area[i].nr_free = 0;
	}

	for (i = 0; i < nr_pages; i++) {
		INIT_LIST_HEAD(&mem_map[i].list);
		mem_map[i].order = PAGE_INUSE;
		mem_map[i]._refcount = 0;
	}

	/* 按最大 2^order 对齐块释放，避免逐页合并 */
	addr = mem_start;
	while (addr < mem_end) {
		idx = (addr - mem_start) / PGSIZE;
		left = (mem_end - addr) / PGSIZE;
		order = 0;
		while (order + 1 < MAX_ORDER &&
		       (idx & ((1U << (order + 1)) - 1)) == 0 &&
		       (1U << (order + 1)) <= left)
			order++;
		__free_one_page(addr_to_page((void *)addr), order);
		addr += PGSIZE << order;
	}

	bytes_to_human(physmem_top, total_size, sizeof(total_size));
	printf("pmm: buddy init, pages=%d free=%d (0x%x-0x%x), mem_map@0x%x (%d KiB), total: %s\n",
	       nr_pages, nr_free, mem_start, mem_end,
	       (uint)mem_map, map_bytes / 1024, total_size);
}

/* 分配 2^order 个连续物理页，返回首页物理地址；失败返回 0 */
void *alloc_pages(unsigned int order)
{
	struct page *page;
	void *addr;

	acquire(&pmm_lock);
	page = __alloc_pages(order);
	if (!page) {
		release(&pmm_lock);
		printk(KERN_ERR "alloc_pages: failed to allocate %d pages\n",
			order);
		return 0;
	}
	addr = page_to_addr(page);
	release(&pmm_lock);

	memset(addr, 0, PGSIZE << order);
	return addr;
}

void put_page(void *addr);	/* 前向声明：free_pages(order0) 使用 */

/* 释放 addr 起的 2^order 页；order 必须与分配时一致 */
void free_pages(void *addr, unsigned int order)
{
	struct page *page;

	if (order >= MAX_ORDER)
		panic("free_pages: bad order");

	/* order 0：走引用计数（COW / 统一 put） */
	if (order == 0) {
		put_page(addr);
		return;
	}

	acquire(&pmm_lock);
	page = addr_to_page(addr);
	if (!page)
		panic("free_pages: bad addr");
	__free_one_page(page, order);
	release(&pmm_lock);
}

void *alloc_page(void)
{
	return alloc_pages(0);
}

void free_page(void *addr)
{
	put_page(addr);
}

/* 增加页引用（fork COW 共享） */
void get_page(void *addr)
{
	struct page *page;

	acquire(&pmm_lock);
	page = addr_to_page(addr);
	if (!page || page->order != PAGE_INUSE)
		panic("get_page");
	page->_refcount++;
	release(&pmm_lock);
}

/* 减少页引用；减至 0 时归还 buddy */
void put_page(void *addr)
{
	struct page *page;

	acquire(&pmm_lock);
	page = addr_to_page(addr);
	if (!page || page->order != PAGE_INUSE)
		panic("put_page");
	if (page->_refcount == 0)
		panic("put_page: refcount 0");
	page->_refcount--;
	if (page->_refcount == 0)
		__free_one_page(page, 0);
	release(&pmm_lock);
}

unsigned int page_refcount(void *addr)
{
	struct page *page;
	unsigned int n;

	acquire(&pmm_lock);
	page = addr_to_page(addr);
	n = page ? page->_refcount : 0;
	release(&pmm_lock);
	return n;
}

unsigned int pmm_nr_free_pages(void)
{
	unsigned int n;

	acquire(&pmm_lock);
	n = nr_free;
	release(&pmm_lock);
	return n;
}

/* buddy 可管理页总数（不含 [0, mem_start) 等不可分配区） */
unsigned int pmm_nr_pages(void)
{
	return nr_pages;
}
