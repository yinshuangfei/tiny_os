/**
 * x86 physical memory layout (QEMU -m 128M)
 *
 * 0x00000000 - 0x000fffff : low memory (BIOS, I/O APIC, legacy I/O)
 * 0x00080000              : kernel boot stack (entry.S)
 * 0x00100000              : kernel load address (KERNBASE)
 * 0x00800000              : end of RAM (PHYSTOP, 128 MiB)
 */

#define PGSIZE     4096

#define KERNBASE   0x00100000
#define PHYSTOP    0x00800000

/** legacy I/O (identity-mapped for MMIO, Memory-Mapped I/O) */
#define IOBASE     0x00000000
#define IOEND      0x00100000
