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
 */
#include "types.h"
#include "defs.h"
#include "memlayout.h"
#include "list.h"
#include "utils.h"

extern char end[];	/* kernel.ld: .bss 结束后的第一个地址 */

#define MAX_ORDER   11	/* 支持 order 0..10，最大单块 2^10 = 1024 页 (4 MiB) */
#define PAGE_INUSE  ((unsigned int)-1)	/* 已分配页标记，不在任何 free_area 链上 */

/*
 * 每个物理页一条元数据，类似 Linux 的 struct page / mem_map。
 * 空闲时挂在 free_area[order] 上；分配后 order = PAGE_INUSE。
 */
struct page {
	struct list_head list;
	unsigned int order;
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
static struct page *mem_map;	/* 指向 page_storage，按页号索引 */
static uint mem_start;		/* 可分配区起始物理地址（页对齐） */
static uint mem_end;		/* 可分配区结束物理地址（= PHYSTOP） */
static unsigned int nr_pages;	/* 可分配页总数 */
static unsigned int nr_free;	/* 当前空闲页总数（所有 order 之和） */

/* 最坏按 [0, PHYSTOP) 每页一条元数据预留 */
static struct page page_storage[PHYSTOP / PGSIZE];

void *memset(void *dst, int c, uint n)
{
	uchar *p = dst;

	while (n-- > 0)
		*p++ = c;
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
		return page;
	}
	return 0;
}

/*
 * 初始化：把 [end, PHYSTOP) 全部以 order=0 逐页释放，
 * buddy 合并会自动拼出更大的连续空闲块。
 * 须在 kvm_init 之前调用（此时尚未开分页，物理地址 == 线性地址）。
 */
void pmm_init(void)
{
	unsigned int i;
	uint addr;
	char total_size[HUMAN_SIZE_MAX];

	mem_start = PGROUNDUP((uint)end);
	mem_end = PHYSTOP;
	nr_pages = (mem_end - mem_start) / PGSIZE;
	mem_map = page_storage;
	nr_free = 0;

	for (i = 0; i < MAX_ORDER; i++) {
		INIT_LIST_HEAD(&free_area[i].free_list);
		free_area[i].nr_free = 0;
	}

	for (i = 0; i < nr_pages; i++) {
		INIT_LIST_HEAD(&mem_map[i].list);
		mem_map[i].order = PAGE_INUSE;
	}

	for (addr = mem_start; addr < mem_end; addr += PGSIZE)
		__free_one_page(addr_to_page((void *)addr), 0);


	bytes_to_human(mem_end, total_size, sizeof(total_size));

	printf("pmm: buddy init, pages=%d free=%d (0x%x-0x%x), total: %s\n",
	       nr_pages, nr_free, mem_start, mem_end, total_size);
}

/* 分配 2^order 个连续物理页，返回首页物理地址；失败返回 0 */
void *alloc_pages(unsigned int order)
{
	struct page *page;

	page = __alloc_pages(order);
	if (!page)
		return 0;
	memset(page_to_addr(page), 0, PGSIZE << order);
	return page_to_addr(page);
}

/* 释放 addr 起的 2^order 页；order 必须与分配时一致 */
void free_pages(void *addr, unsigned int order)
{
	struct page *page;

	if (order >= MAX_ORDER)
		panic("free_pages: bad order");
	page = addr_to_page(addr);
	if (!page)
		panic("free_pages: bad addr");
	__free_one_page(page, order);
}

void *alloc_page(void)
{
	return alloc_pages(0);
}

void free_page(void *addr)
{
	free_pages(addr, 0);
}

unsigned int pmm_nr_free_pages(void)
{
	return nr_free;
}
