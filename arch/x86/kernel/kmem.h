#ifndef __KMEM_H__
#define __KMEM_H__

#include "types.h"

/*
 * 内核小对象堆（基于 alloc_page 的 size-class 分配器）。
 * 供 PCB、链表节点、路径名等动态数据结构使用。
 */
void kmem_init(void);
void *kmalloc(uint size);
void *kcalloc(uint n, uint size);
void kfree(void *ptr);
unsigned int kmem_nr_free(void);

#endif
