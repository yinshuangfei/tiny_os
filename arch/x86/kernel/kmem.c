/*
 * 内核堆：按 2 的幂次 size-class 管理，底层向 buddy 申请整页并切分。
 * 大于最大 class 且不超过一页的请求直接占用一整物理页。
 */
#include "types.h"
#include "defs.h"
#include "memlayout.h"
#include "list.h"
#include "kmem.h"

#define KMAGIC       0x4b4d454d	/* 'KMEM' */
#define KFLAG_PAGE   0x80000000u	/* size 字段标记：整页分配 */

struct khdr {
	struct list_head node;
	uint size;		/* block size, 包含 KFLAG_PAGE 表示整页分配，尾部包含 order */
	uint magic;
};

struct kcache {
	uint bsize;		/* block size */
	struct list_head free;	/* free list */
};

static struct kcache cache[] = {
	{ 32,   { 0, 0 } },
	{ 64,   { 0, 0 } },
	{ 128,  { 0, 0 } },
	{ 256,  { 0, 0 } },
	{ 512,  { 0, 0 } },
	{ 1024, { 0, 0 } },
	{ 2048, { 0, 0 } },
};

#define NCACHE  (sizeof(cache) / sizeof(cache[0]))
static uint nr_kfree;	/* 空闲块数量 */

static int cache_index(uint need)
{
	uint i;

	for (i = 0; i < NCACHE; i++) {
		if (cache[i].bsize >= need)
			return i;
	}
	return -1;
}

/* 从 buddy 系统中分配一个页，并将其切成小块，每个小块的大小为 cache[idx].bsize */
static void cache_refill(int idx)
{
	char *page;
	uint i, n, bsize;
	struct khdr *h;

	bsize = cache[idx].bsize;
	page = alloc_page();
	if (!page)
		panic("kmem: out of memory");

	n = PGSIZE / bsize;
	for (i = 0; i < n; i++) {
		h = (struct khdr *)(page + i * bsize);
		INIT_LIST_HEAD(&h->node);
		list_add(&h->node, &cache[idx].free);
		nr_kfree++;
	}
}

void kmem_init(void)
{
	uint i;

	nr_kfree = 0;
	for (i = 0; i < NCACHE; i++)
		INIT_LIST_HEAD(&cache[i].free);
	printf("kmem: size-class (slab) allocator ready (%d buckets)\n", NCACHE);
}

void *kmalloc(uint size)
{
	struct khdr *h;
	int idx;
	uint need;

	if (size == 0)
		return 0;

	need = size + sizeof(struct khdr);

	/* 如果需要的 size 大于 cache 中最大的 block size，则直接分配一整页 */
	if (need > cache[NCACHE - 1].bsize) {
		uint bytes = PGROUNDUP(need);
		uint order = 0;

		while ((PGSIZE << order) < bytes) {
			order++;
			if (order >= MAX_ORDER)
				panic("kmalloc: size too large");
		}
		h = (struct khdr *)alloc_pages(order);
		if (!h)
			return 0;
		h->size = KFLAG_PAGE | order;
		h->magic = KMAGIC;
		return (void *)(h + 1);
	}

	idx = cache_index(need);
	if (idx < 0)
		panic("kmalloc: bad size");

	if (list_empty(&cache[idx].free))
		cache_refill(idx);

	h = list_first_entry(&cache[idx].free, struct khdr, node);
	list_del(&h->node);
	nr_kfree--;

	h->size = cache[idx].bsize;
	h->magic = KMAGIC;
	return (void *)(h + 1);
}

void *kcalloc(uint n, uint size)
{
	void *p;
	uint total;

	if (n != 0 && size > (uint)-1 / n)
		return 0;

	total = n * size;
	p = kmalloc(total);
	if (p)
		memset(p, 0, total);
	return p;
}

void kfree(void *ptr)
{
	struct khdr *h;
	int idx;

	if (!ptr)
		return;

	h = (struct khdr *)ptr - 1;
	if (h->magic != KMAGIC)
		panic("kfree: bad magic");

	h->magic = 0;

	if (h->size & KFLAG_PAGE) {
		/* 如果 size 包含 KFLAG_PAGE，则表示整页分配，直接释放整页 */
		free_pages(h, h->size & ~KFLAG_PAGE);
		return;
	}

	idx = cache_index(h->size);
	if (idx < 0)
		panic("kfree: bad size");

	list_add(&h->node, &cache[idx].free);
	nr_kfree++;
}

unsigned int kmem_nr_free(void)
{
	return nr_kfree;
}
