// Physical memory layout

// 加载 OS 前物理内存布局
// qemu -machine virt is set up like this,
// based on qemu's hw/riscv/virt.c:
//
// 00001000 -- boot ROM, provided by qemu
// 02000000 -- CLINT, 核心本地中断器
// 0C000000 -- PLIC, 平台级中断控制器
// 10000000 -- uart0, 串口
// 10001000 -- virtio disk, 虚拟磁盘
// 80000000 -- boot ROM jumps here in machine mode
//             -kernel loads the kernel here
// unused RAM after 80000000 (KERNBASE).

// 加载 OS 后物理内存布局
// the kernel uses physical memory thus:
// 80000000 -- entry.S, then kernel text and data
//   .text
// 80007000 -- trampoline/uservec
//   .rodata
//   .data
//   .bss
//      - kernel_pagetable --> 指向分配的页表地址
// end -- start of kernel page allocation area,		内核页分配起始地址
//   [physical page]
//   ...
//   [physical page]
// PHYSTOP -- end RAM used by the kernel		内核页分配结束地址

// qemu puts UART registers here in physical memory.
#define UART0 0x10000000L
#define UART0_IRQ 10

// virtio mmio interface
#define VIRTIO0 0x10001000
#define VIRTIO0_IRQ 1

// local interrupt controller, which contains the timer.
// CLINT: Core Local Interrupter, 核心本地中断器
// 逻辑上，CLINT 是内嵌到每个处理器核心（hart）中的，核心独享
#define CLINT 0x2000000L
#define CLINT_MTIMECMP(hartid) (CLINT + 0x4000 + 8*(hartid))
#define CLINT_MTIME (CLINT + 0xBFF8) // cycles since boot.

// qemu puts programmable interrupt controller here.
// PLIC 的全称是 Platform-Level Interrupt Controller（平台级中断控制器）
#define PLIC 0x0c000000L
#define PLIC_PRIORITY (PLIC + 0x0)
#define PLIC_PENDING (PLIC + 0x1000)
#define PLIC_MENABLE(hart) (PLIC + 0x2000 + (hart)*0x100)
#define PLIC_SENABLE(hart) (PLIC + 0x2080 + (hart)*0x100)
#define PLIC_MPRIORITY(hart) (PLIC + 0x200000 + (hart)*0x2000)
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart)*0x2000)
#define PLIC_MCLAIM(hart) (PLIC + 0x200004 + (hart)*0x2000)
#define PLIC_SCLAIM(hart) (PLIC + 0x201004 + (hart)*0x2000)

// the kernel expects there to be RAM
// for use by the kernel and user pages
// from physical address 0x80000000 to PHYSTOP.
#define KERNBASE 0x80000000L
#define PHYSTOP (KERNBASE + 128*1024*1024) /** KERNBASE + 128MB */

// map the trampoline page to the highest address,
// in both user and kernel space.
// 最高内存地址（逻辑地址）
#define TRAMPOLINE (MAXVA - PGSIZE)

// map kernel stacks beneath the trampoline,
// each surrounded by invalid guard pages.
// 每个内核栈，2*PGSIZE 的大小（逻辑地址）
#define KSTACK(p) (TRAMPOLINE - ((p)+1)* 2*PGSIZE)

// 内核进程内存布局 (kernel_pagetable 描述的地址空间)
// 0x000000            -- Address zero first
// 02000000 + 0x10000  -- CLINT, 核心本地中断器
// 0C000000 + 0x400000 -- PLIC, 平台级中断控制器
// 10000000 + PGSIZE   -- uart0, 串口
// 10001000 + PGSIZE   -- virtio disk, 虚拟磁盘
// KERNBASE 之后
// 80000000 ~ etext    -- 映射内核的 text 段
// etext    ~ PHYSTOP  -- 映射内核的其他段 (128MB 以内)
// ...
// KSTACK(n)   <- CPU2 kernel stack
//   [2*PGSIZE]
// ...
// KSTACK(1)   <- CPU1 kernel stack
//   [2*PGSIZE]
// KSTACK(0)   <- CPU0 kernel stack
//   [2*PGSIZE]
// TRAMPOLINE + PGSIZE
//   [PGSIZE] --> trampoline 的物理地址, 执行状态切换代码
// MAXVA

// 用户进程内存布局 (User memory layout)
// 0x000000            -- Address zero first
//   text
//   original data and bss
// Code-len + PGSIZE   -- user stack guard page
//   stach guard
// stackbase + PGSIZE  -- user stack, fixed-size stack
//   stack data (see kernel/exec.c)
// SP                  -- bottom of stack
// expandable heap
// ...
// TRAPFRAME + PGSIZE  -- p->trapframe, used by the trampoline
//   [PGSIZE] --> proc->trapframe
// TRAMPOLINE + PGSIZE -- the same page as in the kernel
//   [PGSIZE] --> trampoline 的物理地址, 执行状态切换代码
// MAXVA
#define TRAPFRAME (TRAMPOLINE - PGSIZE)