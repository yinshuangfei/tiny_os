/**
 * x86 物理内存布局
 *
 * 0x00000000 - 0x000fffff : 低端内存（BIOS、MMIO）
 * 0x00080000              : 保留低端区域（历史 boot 栈地址，已迁至 BSS）
 * 0x00100000              : 内核加载地址（KERNBASE）
 * physmem_top             : 运行时探测的 RAM 上界（页对齐，受 PHYSMEM_TOP_MAX 约束）
 */

#define PGSIZE     4096

/*
 * buddy 物理页分配器 order 上限：有效 order 为 0 .. MAX_ORDER-1，
 * 最大单块 2^(MAX_ORDER-1) 页（order 10 → 1024 页 = 4 MiB）。
 */
#define MAX_ORDER  11

// 向上取整，将地址向上对齐到 PGSIZE 的倍数
#define PGROUNDUP(sz)   (((sz) + PGSIZE - 1) & ~(PGSIZE - 1))
// 向下取整，将地址向下对齐到 PGSIZE 的倍数
#define PGROUNDDOWN(a)  ((a) & ~(PGSIZE - 1))

/* 内核页目录项数（4KB 页、非 PAE：覆盖完整 4GiB VA） */
#define MAX_KERNEL_PT  1024

#define KERNBASE   0x00100000

/*
 * 用户虚拟地址空间（独立页表，避免与内核恒等映射共用二级页表）：
 * 须位于恒等映射 RAM 之上，否则大内存时 mem_map/buddy 物理地址会落入
 * 用户 VA 窗口，用户页表故意跳过该 PDE 后内核路径会缺页。
 * USERBASE      用户代码起始
 * USERSTACK     用户栈顶（栈向下增长，映射 [USERSTACK-PGSIZE, USERSTACK)）
 */
#define USERBASE     0xC0000000
#define USERSTACK    0xC0400000
#define USEREND      0xC0400000	/* [USERBASE, USEREND) 为用户独占 VA */
/* 堆向上增长，不得进入栈页 [USERSTACK-PGSIZE, USERSTACK) */
#define USERHEAP_TOP (USERSTACK - PGSIZE)

#define KSTACKSIZE   PGSIZE	/* 内核主栈 / 中断栈大小 */
/*
 * (1) 用户态程序，切换到内核态时，内核栈顶的布局：
 * p->kstack + 4096 ─┬─ TSS.esp0（ring3→ring0 硬件从此向下压栈）
 *                   │  trapframe 结束（不含）
 *                   │
 *                   │
 * p->kstack + 4036 ─┼─ struct trapframe (60B)  ← 首次 iret 用，kframe 初值，(tf →)
 *                   │
 * p->kstack + 4032 ─┼─ user_start 伪造返回地址 (4B), user_start_trampoline
 *                   │
 *                   │  ▼ swtch / syscall / 定时器 继续向下用
 *                   │     运行时 trapframe 在栈顶动态构建
 *                   │     timer_trap_user 会更新 p->kframe
 *                   │
 * p->kstack + 0    ─┴─ 页底

 * (2) 纯内核态程序，内核栈顶布局：
 * p->kstack + 4096 ─┬─ 栈顶
 *                   │  [返回地址] → kthread_trampoline
 *                   │  内核线程的栈帧
 * p->kstack + 0    ─┴─ 栈底
*/

#include "../param.h"

extern char kernel_stacks[NR_CPUS][KSTACKSIZE];
extern char interrupt_stacks[NR_CPUS][KSTACKSIZE];
extern unsigned int interrupt_stack_tops[NR_CPUS];

void trapstack_init(void);

#define KERNEL_STACK_TOP    (kernel_stacks[0] + KSTACKSIZE)
#define INTERRUPT_STACK_TOP (interrupt_stacks[0] + KSTACKSIZE)

/*
 * 非 PAE 内核恒等映射下，物理 RAM 可管理上界（页对齐）。
 * 映射区间为 [0, physmem_top)，须 ≤ USERBASE，以免与用户 VA 重叠。
 * 实际大小由 mem_probe() 按机器/QEMU -m 探测。
 */
#define PHYSMEM_TOP_MAX  USERBASE

/** setup.S（E820）与内核共享的引导信息块 */
#define BOOT_INFO        0x5000
#define BOOT_INFO_MAGIC  0x544f4d53	/* 'TOMS' */

/** legacy I/O（恒等映射供 MMIO 使用） */
#define IOBASE     0x00000000
#define IOEND      0x00100000

extern uint physmem_top;
