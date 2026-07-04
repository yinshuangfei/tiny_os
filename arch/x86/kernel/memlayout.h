/**
 * x86 physical memory layout (QEMU -m 128M)
 *
 * 0x00000000 - 0x000fffff : low memory (BIOS, I/O APIC, legacy I/O)
 * 0x00080000              : kernel boot stack (entry.S)
 * 0x00100000              : kernel load address (KERNBASE)
 * 0x00800000              : end of RAM (PHYSTOP, 128 MiB)

   0x00000000 ─┬─ 低端内存、MMIO（也在映射里）
   0x00080000  │  内核栈（SS=0x10 + 页表）
   0x00100000  │  内核代码/数据（CS=0x08 取指，DS=0x10 读写）
   0x00800000 ─┴─ PHYSTOP（映射到此为止）
 */

#define PGSIZE     4096

// 向上取整，将地址向上对齐到 PGSIZE 的倍数
#define PGROUNDUP(sz)   (((sz) + PGSIZE - 1) & ~(PGSIZE - 1))
// 向下取整，将地址向下对齐到 PGSIZE 的倍数
#define PGROUNDDOWN(a)  ((a) & ~(PGSIZE - 1))

#define MAX_KERNEL_PT  1024 	// 内核页表最大数量

#define KERNBASE   0x00100000	// 1 MB
#define PHYSTOP    0x00800000	// 8 MB

/** legacy I/O (identity-mapped for MMIO, Memory-Mapped I/O) */
#define IOBASE     0x00000000
#define IOEND      0x00100000
