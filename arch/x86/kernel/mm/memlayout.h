/**
 * x86 物理内存布局
 *
 * 0x00000000 - 0x000fffff : 低端内存（BIOS、MMIO）
 * 0x00080000              : 保留低端区域（历史 boot 栈地址，已迁至 BSS）
 * 0x00100000              : 内核加载地址（KERNBASE）
 * physmem_top             : 运行时探测的 RAM 上界（<= MAX_PHYSMEM）
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

/* 内核页表项数上限 TODO: 改为懒加载 */
#define MAX_KERNEL_PT  1024

#define KERNBASE   0x00100000

/*
 * 用户虚拟地址空间（独立页表，避免与内核恒等映射共用二级页表）：
 * USERBASE      用户代码起始
 * USERSTACK     用户栈顶（栈向下增长，映射 [USERSTACK-PGSIZE, USERSTACK)）
 */
#define USERBASE     0x00400000

/*
 * 用户栈顶，栈向下增长，映射 [USERSTACK-PGSIZE, USERSTACK)。
 * 用户程序有两个栈：用户栈和内核栈。用户栈是用户程序的栈，内核栈切换到内核使用的栈。
 */
#define USERSTACK    0x00800000
#define USEREND      0x00800000	/* [USERBASE, USEREND) 为用户独占 VA */

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

extern char kernel_stack[KSTACKSIZE];
extern char interrupt_stack[KSTACKSIZE];

#define KERNEL_STACK_TOP    (kernel_stack + KSTACKSIZE)
#define INTERRUPT_STACK_TOP (interrupt_stack + KSTACKSIZE)

/*
 * page_storage 等元数据数组上限；实际 RAM 由 mem_probe() 探测，
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
