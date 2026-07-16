#ifndef __ELF_H__
#define __ELF_H__

#include "types.h"

/*
 * ELF32（i386）可执行文件：仅解析装载所需的文件头与程序头。
 * 字节序：小端；与 Linux / xv6 布局一致。
 */

#define ELF_MAGIC 0x464C457FU /* \x7fELF */

#define ET_EXEC 2
#define EM_386  3

#define ELF_PROG_LOAD 1

#define ELF_PROG_FLAG_EXEC  1
#define ELF_PROG_FLAG_WRITE 2
#define ELF_PROG_FLAG_READ  4

/* 文件头 */
struct elfhdr {
	uint magic;		/* 须为 ELF_MAGIC */
	uchar elf[12];
	ushort type;		/* 类型 */
	ushort machine;		/* 机器类型 */
	uint version;		/* 版本 */
	uint entry;		/* 入口虚地址 */
	uint phoff;		/* 程序头表文件偏移 */
	uint shoff;		/* 节区头表文件偏移 */
	uint flags;		/* 标志 */
	ushort ehsize;		/* 文件头大小 */
	ushort phentsize;	/* 程序头大小 */
	ushort phnum;		/* 程序头数量, n 个 proghdr */
	ushort shentsize;	/* 节区头大小 */
	ushort shnum;		/* 节区头数量 */
	ushort shstrndx;	/* 节区头字符串表索引 */
};

/* 程序头 */
struct proghdr {
	uint type;		/* ELF_PROG_LOAD 等类型 */
	uint off;		/* 段在文件中的偏移 */
	uint vaddr;		/* 装载虚地址 */
	uint paddr;		/* 装载物理地址 */
	uint filesz;		/* 文件中有效字节数 */
	uint memsz;		/* 内存中占用大小（可 > filesz，多出为零） */
	uint flags;		/* R/W/X 标志 */
	uint align;		/* 对齐 */
};

/* ELF 辅助向量类型（Linux / SysV i386 ABI，create_elf_tables） */
#define AT_NULL		0
#define AT_IGNORE	1
#define AT_EXECFD	2
#define AT_PHDR		3
#define AT_PHENT	4
#define AT_PHNUM	5
#define AT_PAGESZ	6
#define AT_BASE		7
#define AT_FLAGS	8
#define AT_ENTRY	9
#define AT_UID		11
#define AT_EUID		12
#define AT_GID		13
#define AT_EGID		14

#endif
