/**
 * x86 32 位地址空间布局（Linux 风格 3G/1G split）
 *
 * 虚拟地址：
 *   0x00000000 - 0xBFFFFFFF : 用户空间
 *   0xC0000000 - 0xFFFFFFFF : 内核空间
 *
 * 物理地址：
 *   0x00000000 - 0x000FFFFF : 低端内存（BIOS、VGA、trampoline 等）
 *   0x00100000              : 内核物理加载地址
 *   physmem_top             : 运行时探测的 RAM 上界（页对齐，受 PHYSMEM_TOP_MAX 约束）
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

#define KERNEL_LOAD_PA       0x00100000u
#define KERNEL_HIGH_LOAD_PA  0x00102000u
#define KERNBASE             0xC0000000u
#define KERNLINK             (KERNBASE + KERNEL_HIGH_LOAD_PA)

/*
 * 低端恒等映射窗口：
 * 仅供 BSP/AP 早期启动、BIOS warm-reset 向量、VGA 文本显存等使用。
 * 该恒等映射保留在 kernel_pgdir 中，但不会复制到用户页表。
 */
#define KERNEL_BOOT_IDMAP_END 0x00400000u

/* 低端 RAM 的内核 direct-map：kva = pa + KERNBASE */
#define P2V(pa)              ((uint)(pa) + KERNBASE)
#define V2P(va)              ((uint)(va) - KERNBASE)

/*
 * 用户虚拟地址空间：
 * USERBASE          用户地址空间下界
 * USERLOAD          用户程序默认装载基址（保留低页，避免 NULL 附近映射）
 * USEREND           用户空间上界（开区间）/ 内核起始
 * USERSTACK         用户栈顶（栈向下增长）
 * USERSTACK_BOTTOM  预映射用户栈底
 * USERHEAP_TOP      heap/mmap 可用上界（为栈和 guard 留空）
 */
#define USERBASE            0x00000000u
#define USERLOAD            0x00400000u
#define USEREND             KERNBASE
#define USERSTACK           USEREND
#define USER_STACK_PAGES    16u		/* 64 KiB 用户栈 */
#define USER_STACK_SIZE     (USER_STACK_PAGES * PGSIZE)
#define USER_STACK_GUARD    (16u * PGSIZE)
#define USERSTACK_BOTTOM    (USERSTACK - USER_STACK_SIZE)
/* 堆向上增长，mmap 自高向低，均不得进入 guard/stack 区 */
#define USERHEAP_TOP        (USERSTACK_BOTTOM - USER_STACK_GUARD)

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
 * 非 PAE + 3G/1G split 下，低端 RAM 的可 direct-map 上界（页对齐）。
 * 这里保留最高 128 MiB 给高端 MMIO / 固定内核映射窗口，等价于 Linux
 * 传统 896 MiB lowmem 的教学缩略版。
 * 实际大小由 mem_probe() 按机器/QEMU -m 探测。
 */
#define PHYSMEM_TOP_MAX      0x38000000u

/** setup.S（E820）与内核共享的引导信息块 */
#define BOOT_INFO        0x5000
#define BOOT_INFO_MAGIC  0x544f4d53	/* 'TOMS' */

/** legacy I/O（恒等映射供 MMIO 使用） */
#define IOBASE     0x00000000
#define IOEND      0x00100000

extern uint physmem_top;
