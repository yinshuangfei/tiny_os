// Format of an ELF executable file

#ifndef _elf_h_
#define _elf_h_

#include "types.h"

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian

// File header
struct elfhdr {
	uint magic;  // must equal ELF_MAGIC
	uchar elf[12];
	ushort type;
	ushort machine;
	uint version;
	uint64 entry;		// 入口地址
	uint64 phoff;		// Program section header 偏移
	uint64 shoff;
	uint flags;
	ushort ehsize;
	ushort phentsize;
	ushort phnum;		// Program section header 数量
	ushort shentsize;
	ushort shnum;
	ushort shstrndx;
}; /** 64 bytes */

// Program section header
struct proghdr {
	uint32 type;
	uint32 flags;
	uint64 off;		// offset
	uint64 vaddr;		// 虚拟地址, PGSIZE 对齐
	uint64 paddr;		// 物理地址
	uint64 filesz;		// file size
	uint64 memsz;		// mem size
	uint64 align;
}; /** 56 bytes */

// Values for Proghdr type
#define ELF_PROG_LOAD           1

// Flag bits for Proghdr flags
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4

#endif /** _elf_h_ */