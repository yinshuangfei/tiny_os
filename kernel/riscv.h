/**
 * # 开机
 * 在 RISC-V 架构中，系统开机启动时的特权模式及切换机制如下：
 * - 1.开机启动模式：机器模式（M-mode）
 * - 2.发生切换的操作与机制
 *     当 Bootloader 或固件在 M-mode 下完成了最基础的硬件初始化（如时钟配置、DDR内存
 * 初始化等）后，需要将执行权限移交给运行在监督模式（S-mode）的操作系统内核。这一切换
 * 操作是通过执行 mret（Machine Return）指令来实现的。
 *
 * # 触发 Trap
 * 在 RISC-V 架构中，用户程序触发 Trap（陷阱，包含系统调用、异常和中断）是一个硬件与
 * 操作系统软件高度协同的过程。RISC-V 的设计哲学是“硬件做减法，软件做加法”，即硬件只
 * 负责最基础的现场记录和权限切换，而复杂的上下文保存、页表切换和具体处理逻辑则交由操作
 * 系统软件完成.
 * 整个机制可以清晰地划分为以下四个阶段：
 * 1. 硬件自动操作阶段
 *    当用户态程序执行 ecall（系统调用）、发生非法指令或访问违规内存时，RISC-V 硬件会
 * 强制接管 CPU 控制权，并自动执行以下操作：
 * - 禁用中断：清除 sstatus 寄存器中的 SIE 位，防止在处理 Trap 时被其他中断打断16。
 * - 保存现场线索：将当前的程序计数器（PC）复制到 sepc 寄存器中，以便后续返回。
 * - 记录状态与原因：将当前的特权级（用户模式）记录到 sstatus 的 SPP 位中；同时将触发
 * Trap 的具体原因（如系统调用、页错误等）写入 scause 寄存器。
 * - 切换特权级：将 CPU 模式从用户模式（U-mode）切换为监督模式（S-mode）。
 * - 跳转处理程序：将 stvec（Supervisor Trap Vector）寄存器中预设的地址加载到 PC 中，
 * CPU 随即跳转到该地址开始执行。
 * 注意：此时 CPU 不会自动切换到内核页表，不会切换到内核栈，也不会保存除 PC 以外的任何
 * 通用寄存器。
 * 2. 汇编入口与上下文切换阶段（Trampoline）
 *    由于硬件没有切换页表和栈，操作系统必须在用户虚拟地址空间的顶部预先映射一段特殊的
 * 汇编代码（通常称为 trampoline 页，如 uservec）。
 * - 临时保存：uservec 利用 sscratch 寄存器（该寄存器在进入用户态前已被内核设置为当前
 * 进程的 trapframe 地址）来暂存数据，将用户态的 32 个通用寄存器全部保存到 trapframe 中。
 * - 切换环境：从 trapframe 中读取内核栈指针、内核页表根地址（satp）等信息，修改 satp
 * 寄存器切换到内核页表，并将栈指针（SP）指向内核栈。
 * - 进入 C 语言处理：最后跳转到内核的 C 语言 Trap 处理函数（如 usertrap()）。
 * 3. 内核 C 语言处理阶段
 *    进入 usertrap() 后，操作系统开始根据 scause 寄存器的值分发并处理具体的 Trap 事件：
 * - 系统调用 (ecall)：从 trapframe 中提取系统调用号（通常在 a7 寄存器）和参数，查表
 * 调用对应的内核函数。处理完成后，会将返回值写入 trapframe 的 a0 位置，并将 sepc 加
 * 4（跳过 ecall 指令）。
 * - 设备中断：调用设备驱动处理中断，或者触发进程调度（如时钟中断导致当前进程让出 CPU）。
 * - 异常（如缺页、非法指令）：内核会检查异常原因。如果是可恢复的缺页，内核会分配物理页
 * 并建立映射；如果是无法恢复的致命错误，内核会向该进程发送信号（如 SIGSEGV）或直接终止
 * 该进程。
 * 4. 返回用户态阶段
 * Trap 处理完毕后，内核需要安全地返回用户空间，这通常由 usertrapret() 和汇编代码
 * userret 配合完成：
 * - 准备返回：usertrapret() 会将 stvec 重新指向用户态的 uservec（为下一次 Trap 做
 * 准备），并将内核状态和寄存器信息写回 trapframe。
 * - 恢复上下文：userret 汇编代码将页表切换回用户页表，从 trapframe 中恢复所有 32 个
 * 用户态通用寄存器。
 * - 执行 sret 指令：最后执行 sret 指令，硬件会自动从 sepc 恢复 PC，并根据 sstatus
 * 中的 SPP 位将特权级降回用户模式，程序继续执行。
 *
 * risc-v 中，用户态出现异常、ecall 或者页错误这类行为触发的trap，这种行为是不能禁止的，
 * 因为这是硬件级别的强制行为.
 * - 不可屏蔽性：用户态没有任何特权级或指令可以“关闭”或“忽略”这些硬件异常。
 * - 全局中断使能的局限：虽然 sstatus 寄存器中有 SIE（Supervisor Interrupt Enable）
 * 位，但它仅仅对中断（Interrupts）有效，对异常（Exceptions，如缺页、非法指令）和系统
 * 调用（ecall）完全无效。
 * (页错误（Page Fault）完全由硬件触发。)
 *
 * 在单核 CPU 中，程序陷入了 panic 执行死循环空转（panic 未关闭中断），还能响应中断,
 * 中断上下文依然保存在当前进程的内核栈（Kernel Stack）中.
 *
 * 在 RISC-V 架构中，用户态程序（U-mode）绝对不能控制中断的开启和关闭.
 */
#ifndef __riscv_h__
#define __riscv_h__

#include "types.h"

static inline uint64 r_mhartid()
{
	uint64 x;
	asm volatile("csrr %0, mhartid" : "=r" (x) );
	return x;
}

// Machine Status Register, mstatus
#define MSTATUS_MPP_MASK (3L << 11) // previous mode.
#define MSTATUS_MPP_M (3L << 11)
#define MSTATUS_MPP_S (1L << 11)
#define MSTATUS_MPP_U (0L << 11)
#define MSTATUS_MIE (1L << 3)    // machine-mode interrupt enable.

// mstatus（Machine Status Register，机器状态寄存器）是 RISC-V 中最核心的 CSR 之一，
// 它控制着处理器的全局状态。修改它的值会直接影响 CPU 的行为。
// 它包含的关键位（Bits）主要有：
// - MIE (Machine Interrupt Enable)：全局机器中断使能位。置 1 时允许响应机器级中断，
// 清 0 时屏蔽中断。
// - MPIE (Machine Previous Interrupt Enable)：保存进入异常前的 MIE 状态，用于异常
// 返回时恢复。
// - MPP (Machine Previous Privilege)：记录进入异常前的特权级（Machine、Supervisor
// 或 User）。当执行 mret 指令时，CPU 会根据 MPP 的值决定返回到哪个特权级。
static inline uint64 r_mstatus()
{
	uint64 x;
	asm volatile("csrr %0, mstatus" : "=r" (x));
	return x;
}

static inline void w_mstatus(uint64 x)
{
	asm volatile("csrw mstatus, %0" : : "r" (x));
}

// machine exception program counter (mepc), holds the
// instruction address to which a return from
// exception will go.
// Machine Exception Program Counter，机器异常程序计数器
// 当 CPU 发生中断或异常（例如系统调用、缺页异常）时，硬件会自动将发生异常时的下一条
// 指令的地址保存到 mepc 中。
// 当操作系统处理完异常，执行 mret 指令返回时，CPU 会读取 mepc 的值，并跳转到该地址
// 继续执行。
// csrw：CSR Write 的伪指令，用于将通用寄存器的值写入 CSR 寄存器
//       CSR: Control and Status Register（控制与状态寄存器）
static inline void w_mepc(uint64 x)
{
	asm volatile("csrw mepc, %0" : : "r" (x));
}

// Supervisor Status Register, sstatus
#define SSTATUS_SPP (1L << 8)  // Previous mode, 1=Supervisor, 0=User
// 中断前的状态备份
// 作用：它是 SIE 发生中断前的“状态快照”。作为一个“状态快照”，专门用来记录在发生异常或
//       中断（Trap）之前，S-mode 的中断总开关（SIE位）是打开还是关闭的。
// 行为：当 Trap 发生时，硬件会把 SIE 原来的值复制到 SPIE 中保存起来。
// 恢复机制：当内核处理完异常，执行 sret（Supervisor Return）指令准备返回被中断的代码
//          时，硬件会自动把 SPIE 的值复制回 SIE。这样，中断发生前如果中断是打开的，
//          返回后中断依然是打开的。
#define SSTATUS_SPIE (1L << 5) // Supervisor Previous Interrupt Enable
#define SSTATUS_UPIE (1L << 4) // User Previous Interrupt Enable
// 当前的中断总开关
// - SIE = 1 时，CPU 允许响应被委托给 S-mode 的中断（例如定时器中断、外设中断等）。
// - SIE = 0 时，CPU 会屏蔽（忽略）所有的 S-mode 中断。
#define SSTATUS_SIE (1L << 1)  // Supervisor Interrupt Enable
#define SSTATUS_UIE (1L << 0)  // User Interrupt Enable

static inline uint64 r_sstatus()
{
	uint64 x;
	asm volatile("csrr %0, sstatus" : "=r" (x) );
	return x;
}

static inline void w_sstatus(uint64 x)
{
	asm volatile("csrw sstatus, %0" : : "r" (x));
}

// Supervisor Interrupt Pending
static inline uint64 r_sip()
{
	uint64 x;
	asm volatile("csrr %0, sip" : "=r" (x) );
	return x;
}

static inline void w_sip(uint64 x)
{
	asm volatile("csrw sip, %0" : : "r" (x));
}

// use riscv's sv39 page table scheme.
#define SATP_SV39 (8L << 60)

#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)pagetable) >> 12))

// Supervisor Address Translation and Protection;
// holds the address of the page table.
// 设置页表功能的开启和关闭
static inline void w_satp(uint64 x)
{
	asm volatile("csrw satp, %0" : : "r" (x));
}

// 获取页表寄存器值
static inline uint64 r_satp()
{
	uint64 x;
	asm volatile("csrr %0, satp" : "=r" (x) );
	return x;
}

// Supervisor Scratch register, for early trap handler in trampoline.S.
// 它的核心作用是在 Trap（异常、中断或系统调用）发生的最早期，
// 协助操作系统安全地保存和切换上下文
static inline void w_sscratch(uint64 x)
{
	asm volatile("csrw sscratch, %0" : : "r" (x));
}

// Machine Scratch Register
// mscratch 专门服务于最高特权级（如裸机环境、Bootloader 或负责底层硬件管理的固件）
static inline void w_mscratch(uint64 x)
{
	asm volatile("csrw mscratch, %0" : : "r" (x));
}

// Supervisor Trap Cause
// scause 的全称是 Supervisor Cause Register
// 当发生异常或中断（Trap）时，由硬件自动记录下导致本次陷入（Trap）的具体原因
/**
 * scause（Supervisor Cause Register）用于记录导致 CPU 陷入（Trap）监督模式
 * （S-mode）的具体原因。
 * scause 寄存器的最高位（Interrupt 位）用于区分是中断还是异常：
 * - (1) 为 1 时，表示触发来源是中断（Interrupt）
 *   中断通常由外部硬件事件或定时器触发：
	1：用户软件中断（User software interrupt）
	5：监督模式软件中断（Supervisor software interrupt）
	9：监督模式外部中断（Supervisor external interrupt）
	4：用户定时器中断（User timer interrupt）
	5：监督模式定时器中断（Supervisor timer interrupt）
	8：用户外部中断（User external interrupt）

 * - (2) 为 0 时，表示触发来源是异常（Exception）
 *   异常通常由指令执行过程中的错误、非法操作或环境调用引起。其中与操作系统内存管理最
 *   密切的是页错误（Page Fault）：
	(1) 常见异常与系统调用：
	2：非法指令（Illegal instruction）
	3：断点（Breakpoint，如 ebreak 指令触发）
	8：用户模式环境调用（Environment call from U-mode，即系统调用）
	9：监督模式环境调用（Environment call from S-mode）
	(2) 页错误（Page Faults）：
	12：指令页错误（Instruction page fault，取指时发生页错误）
	13：加载页错误（Load page fault，执行 load 指令时发生页错误）
	15：存储/原子操作页错误（Store/AMO page fault，执行 store 指令时发生页错误）
	(3) 内存访问与对齐错误：
	0：指令地址未对齐（Instruction address misaligned）
	1：指令访问错误（Instruction access fault）
	4：加载地址未对齐（Load address misaligned）
	5：加载访问错误（Load access fault）
	6：存储/原子操作地址未对齐（Store/AMO address misaligned）
	7：存储/原子操作访问错误（Store/AMO access fault）
 */
static inline uint64 r_scause()
{
	uint64 x;
	asm volatile("csrr %0, scause" : "=r" (x) );
	return x;
}

// Supervisor Trap Value
// 提供出错的虚拟内存地址
static inline uint64 r_stval()
{
	uint64 x;
	asm volatile("csrr %0, stval" : "=r" (x) );
	return x;
}

// Machine-mode Counter-Enable
static inline void w_mcounteren(uint64 x)
{
	asm volatile("csrw mcounteren, %0" : : "r" (x));
}

static inline uint64 r_mcounteren()
{
	uint64 x;
	asm volatile("csrr %0, mcounteren" : "=r" (x) );
	return x;
}

// machine-mode cycle counter
static inline uint64 r_time()
{
	uint64 x;
	asm volatile("csrr %0, time" : "=r" (x) );
	return x;
}

// Supervisor Interrupt Enable
#define SIE_SEIE (1L << 9) // external
#define SIE_STIE (1L << 5) // timer
#define SIE_SSIE (1L << 1) // software
static inline uint64 r_sie()
{
	uint64 x;
	asm volatile("csrr %0, sie" : "=r" (x) );
	return x;
}

static inline void w_sie(uint64 x)
{
	asm volatile("csrw sie, %0" : : "r" (x));
}

// Machine-mode Interrupt Enable
#define MIE_MEIE (1L << 11) // external
#define MIE_MTIE (1L << 7)  // timer
#define MIE_MSIE (1L << 3)  // software
static inline uint64 r_mie()
{
	uint64 x;
	asm volatile("csrr %0, mie" : "=r" (x) );
	return x;
}

static inline void w_mie(uint64 x)
{
	asm volatile("csrw mie, %0" : : "r" (x));
}

// machine exception program counter, holds the
// instruction address to which a return from
// exception will go.
// sepc 的全称是 Supervisor Exception Program Counter
// 当发生异常或中断（Trap）时，保存被中断程序的下一条指令的地址（即程序计数器 PC 的值），
// 以便处理完毕后能够准确返回.
// - 对于普通的同步异常（如 ecall），sepc 保存的是触发异常的那条指令的地址；
// - 对于异步中断，sepc 保存的是下一条将要执行但还没执行的指令的地址;
static inline void w_sepc(uint64 x)
{
	asm volatile("csrw sepc, %0" : : "r" (x));
}

static inline uint64 r_sepc()
{
	uint64 x;
	asm volatile("csrr %0, sepc" : "=r" (x) );
	return x;
}

// Machine Exception Delegation
static inline uint64 r_medeleg()
{
	uint64 x;
	asm volatile("csrr %0, medeleg" : "=r" (x) );
	return x;
}

static inline void w_medeleg(uint64 x)
{
	asm volatile("csrw medeleg, %0" : : "r" (x));
}

// Machine Interrupt Delegation
static inline uint64 r_mideleg()
{
	uint64 x;
	asm volatile("csrr %0, mideleg" : "=r" (x) );
	return x;
}

static inline void w_mideleg(uint64 x)
{
	asm volatile("csrw mideleg, %0" : : "r" (x));
}

// Supervisor Trap-Vector Base Address
// low two bits are mode.
// stvec 是 Supervisor Trap Vector Base Address，CPU 发生 trap 时会跳到这里
/**
 * stvec 的值被拆分为两部分:
 * - 低 2 位 (Bits 1:0): MODE, 模式位
 * 	(1) MODE = 00: 直接模式 (Direct Mode)
 * 	所有的中断和异常发生时，CPU 无条件直接跳转到 BASE 地址执行
 * 	(2) MODE = 01: 向量模式 (Vectored Mode)
 * 	- 如果是同步异常（如非法指令、缺页），CPU 依然跳转到 BASE 地址
 * 	- 如果是异步中断（如定时器中断、外部中断），CPU 会跳转到 BASE +
 * 	  (Exception Code × 4) 的地址
 * - 高位 (剩余所有位): BASE, 基地址
 */
static inline void w_stvec(uint64 x)
{
	asm volatile("csrw stvec, %0" : : "r" (x));
}

static inline uint64 r_stvec()
{
	uint64 x;
	asm volatile("csrr %0, stvec" : "=r" (x) );
	return x;
}

// Machine-mode interrupt vector
static inline void w_mtvec(uint64 x)
{
	asm volatile("csrw mtvec, %0" : : "r" (x));
}


// enable device interrupts
static inline void intr_on()
{
	w_sstatus(r_sstatus() | SSTATUS_SIE);
}

// disable device interrupts
static inline void intr_off()
{
	w_sstatus(r_sstatus() & ~SSTATUS_SIE);
}

// are device interrupts enabled?
static inline int intr_get()
{
	uint64 x = r_sstatus();
	return (x & SSTATUS_SIE) != 0;
}

static inline uint64 r_sp()
{
	uint64 x;
	asm volatile("mv %0, sp" : "=r" (x) );
	return x;
}

// read and write tp, the Thread Pointer, which holds
// this core's hartid (core number), the index into cpus[].
// Thread Pointer（线程指针）, 存储当前正在运行的线程/任务的局部存储区域的基地址
// 硬件编号：x4
/**
 * 获取当前 CPU ID
 * 在进行任务切换时，为了保证当前线程的状态不丢失，操作系统会将 tp 寄存器的值保存到当前
 * 任务的上下文结构（Task Context）中；当调度器切换到下一个任务时，再从新任务的上下文中
 * 恢复 tp 的值
 */
static inline uint64 r_tp()
{
	uint64 x;
	asm volatile("mv %0, tp" : "=r" (x) );
	return x;
}

static inline void w_tp(uint64 x)
{
	asm volatile("mv tp, %0" : : "r" (x));
}

// Return Address（返回地址） 寄存器的缩写
// 硬件编号: x1
// ra 在每次 call/jal 之后都会被改写
static inline uint64 r_ra()
{
	uint64 x;
	asm volatile("mv %0, ra" : "=r" (x) );
	return x;
}

// flush the TLB.
static inline void sfence_vma()
{
	// the zero, zero means flush all TLB entries.
	asm volatile("sfence.vma zero, zero");
}

#define PGSIZE 4096 // bytes per page
#define PGSHIFT 12  // bits of offset within a page

/** 向上对齐 1 -> PGSIZE */
#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))
/** 向下对齐 1 -> 0 */
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))

#define PTE_V (1L << 0) // valid
#define PTE_R (1L << 1)	// read
#define PTE_W (1L << 2) // write
#define PTE_X (1L << 3) // exec
#define PTE_U (1L << 4) // 1 -> user can access

/**
 * https://docs.riscv.org/reference/isa/v20260120/priv/supervisor.html#addressing-and-memory-protection
 * 在 RISC-V 的 Sv39 方案中，一个 64 位的 PTE 内部结构如下：
 * (1) va: Sv39 virtual address(39 bit)：
 * ---------------------------------------------------
 * vpn[2],9b | vpn[1],9b | vpn[0],9b | page offset,12b
 * ---------------------------------------------------
 * - 0-11(12 bit): page offset
 * - 12-20(9 bit): vpn[0]
 * - 21-29(9 bit): vpn[1]
 * - 30-38(9 bit): vpn[2]
 *
 * (2) pte: page table entry
 * --------------------------------------
 * |  高 10  |        中 44      | 低 10 |
 * --------------------------------------
 *    保留        物理页号         标志位
 * - 低 10 位（位 9~0）：
 * 	0: V, 有效位
 * 	1: R, 读
 * 	2: W, 写
 * 	3: X, 执行
 * 	4: U
 * 	5: G
 * 	6: A
 * 	7: D
 * 	8-9: 保留
 * - 中 44 位（位 53~10）：物理页号（PPN, Physical Page Number）
 * 	10-18(9 bit)：PPN[0]
 * 	19-27(9 bit)：PPN[1]
 * 	28-53(26 bit)：PPN[2]
 * - 高 10 位：
 * 	54-60(7 bit)：保留
 * 	61-62(2 bit)：PBMT
 * 	63:: N
 *
 * (3) pa: physical address
 * ---------------------------------------------------
 * ppn[2],26b | ppn[1],9b | ppn[0],9b | page offset,12b
 * ---------------------------------------------------
 * - 0-11(12 bit): page offset
 * - 12-20(9 bit): ppn[0]
 * - 21-29(9 bit): ppn[1]
 * - 30-55(26 bit): ppn[2]
 */
// shift a physical address to the right place for a PTE.
// 物理地址 -> pte
#define PA2PTE(pa) ((((uint64)pa) >> 12) << 10)
// (pte) >> 10, 清除低 10 位的标志位（Flags）
// << 12, 将物理页号还原为真实的物理内存基地址
// 这意味着任何一个物理页的起始地址，其最低的 12 位必然全是 0（即 4KB 对齐）
// pte -> 物理地址
#define PTE2PA(pte) (((pte) >> 10) << 12)
// 10 位标志位
#define PTE_FLAGS(pte) ((pte) & 0x3FF)

// extract the three 9-bit page table indices from a virtual address.
#define PXMASK          0x1FF // 9 bits
#define PXSHIFT(level)  (PGSHIFT+(9*(level)))
// 返回 level-x 的 9bit 值
#define PX(level, va) ((((uint64) (va)) >> PXSHIFT(level)) & PXMASK)

// one beyond the highest possible virtual address.
// MAXVA is actually one bit less than the max allowed by
// Sv39, to avoid having to sign-extend virtual addresses
// that have the high bit set.
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))

typedef uint64 pte_t;
typedef uint64 *pagetable_t; // 512 PTEs

#endif /** __riscv_h__ */
