/**
 * x86 物理内存布局
 *
 * 0x00000000 - 0x000fffff : 低端内存（BIOS、MMIO）
 * 0x00080000              : 内核启动栈（entry.S）
 * 0x00100000              : 内核加载地址（KERNBASE）
 * physmem_top             : 运行时探测的 RAM 上界（<= MAX_PHYSMEM）
 */

#define PGSIZE     4096

// 向上取整，将地址向上对齐到 PGSIZE 的倍数
#define PGROUNDUP(sz)   (((sz) + PGSIZE - 1) & ~(PGSIZE - 1))
// 向下取整，将地址向下对齐到 PGSIZE 的倍数
#define PGROUNDDOWN(a)  ((a) & ~(PGSIZE - 1))

#define MAX_KERNEL_PT  1024

#define KERNBASE   0x00100000

/*
 * page_storage 等元数据数组上限；实际 RAM 由 mem_init() 探测，
 * 结果存入 physmem_top，且 physmem_top <= MAX_PHYSMEM。
 */
#define MAX_PHYSMEM  0x08000000	/* 128 MiB */

/** setup.S（E820）与内核共享的引导信息块 */
#define BOOT_INFO        0x5000
#define BOOT_INFO_MAGIC  0x544f4d53	/* 'TOMS' */

/** legacy I/O（恒等映射供 MMIO 使用） */
#define IOBASE     0x00000000
#define IOEND      0x00100000

extern uint physmem_top;
