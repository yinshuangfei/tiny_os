#include "kernel/types.h"
#include "kernel/stat.h"
#include "lib/user.h"
#include "kernel/param.h"

// Memory allocator by Kernighan and Ritchie,
// The C programming Language, 2nd ed.  Section 8.7.
/**
 * K&R 经典的“空闲链表”分配器的原理:
 * 将空闲内存块通过指针串联成一个单向链表，分配时从链表中摘除节点，释放时将节点重新插入
 * 链表。
 *
 * - 每块内存前面都带一个 Header
 * - Header 里记录这块有多大、下一块空闲块在哪
 * - 所有空闲块按地址顺序串成一个循环链表
 * - malloc() 在链表里找一块足够大的空闲块
 * - free() 把释放的块重新插回链表，并尝试和左右相邻空闲块合并
 */

typedef long Align;

union header {
	struct {
		union header *ptr;	// 8  Bytes, 下一个块的地址
		uint size;		// 4  Bytes, Header 个数
	} s;				// 16 Bytes (按最长单元对齐)
	Align x;			// 8  Bytes
}; /** 16 Bytes */

typedef union header Header;

static Header base;
static Header *freep;

/**
 * - 如果 bp 紧挨着后一个空闲块，就先把两块合并。
 * - 如果前一个空闲块紧挨着 bp，再继续合并。
 * - 最后更新 freep。
 */
void free(void *ap)
{
	Header *bp, *p;

	bp = (Header*)ap - 1;
	for (p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
		if (p >= p->s.ptr && (bp > p || bp < p->s.ptr))
			break;

	if (bp + bp->s.size == p->s.ptr) {
		bp->s.size += p->s.ptr->s.size;
		bp->s.ptr = p->s.ptr->s.ptr;
	} else {
		bp->s.ptr = p->s.ptr;
	}

	if (p + p->s.size == bp) {
		p->s.size += bp->s.size;
		p->s.ptr = bp->s.ptr;
	} else {
		p->s.ptr = bp;
	}
	freep = p;
}

static Header* morecore(uint nu)
{
	char *p;
	Header *hp;

	if (nu < 4096)
		nu = 4096;
	p = sbrk(nu * sizeof(Header));
	if (p == (char*)-1)
		return 0;
	hp = (Header*)p;
	hp->s.size = nu;
	/** 空出一个块 */
	free((void*)(hp + 1));
	return freep;
}

/**
 * 在现代 C 语言编程中，我们通常不直接调用 sbrk，而是使用更高级的内存分配函数
 * （如 malloc 和 free）。但在底层，malloc 正是通过调用 sbrk（或者 mmap）来向操作系统
 * 申请扩大堆空间的。
 */
void* malloc(uint nbytes)
{
	Header *p, *prevp;
	uint nunits;

	// 计算块数
	nunits = (nbytes + sizeof(Header) - 1)/sizeof(Header) + 1;

	if ((prevp = freep) == 0) {
		base.s.ptr = freep = prevp = &base;
		base.s.size = 0;
	}
	for (p = prevp->s.ptr; ; prevp = p, p = p->s.ptr) {
		if (p->s.size >= nunits) {
			/** 大小相等，摘下块 */
			if (p->s.size == nunits) {
				prevp->s.ptr = p->s.ptr;
			} else {
				p->s.size -= nunits;
				p += p->s.size;
				p->s.size = nunits;
			}
			freep = prevp;
			return (void*)(p + 1);
		}
		if (p == freep)
			if((p = morecore(nunits)) == 0)
				return 0;
	}
}
