/*
 * mmap(2)：包装 mmap2（offset 转为页偏移，对齐 Linux i386）。
 */
#include "user.h"

/*
 * addr: 映射的地址，
 * length: 映射的长度，
 * prot: 保护标志，
 * flags: 标志位，
 * fd: 文件描述符，
 * offset: 文件偏移量(字节)
 */
void *mmap2(void *addr, unsigned int length, int prot, int flags,
	    int fd, unsigned int pgoff);

void *mmap(void *addr, unsigned int length, int prot, int flags,
	   int fd, int offset)
{
	if (offset & 0xfff)
		return MAP_FAILED;
	return mmap2(addr, length, prot, flags, fd, (unsigned int)offset >> 12);
}
