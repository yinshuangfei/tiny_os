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

static inline uint64 r_satp()
{
	uint64 x;
	asm volatile("csrr %0, satp" : "=r" (x) );
	return x;
}

// Supervisor Scratch register, for early trap handler in trampoline.S.
static inline void w_sscratch(uint64 x)
{
	asm volatile("csrw sscratch, %0" : : "r" (x));
}

static inline void w_mscratch(uint64 x)
{
	asm volatile("csrw mscratch, %0" : : "r" (x));
}

// Supervisor Trap Cause
// scause 的全称是 Supervisor Cause Register
// 当发生异常或中断（Trap）时，由硬件自动记录下导致本次陷入（Trap）的具体原因
static inline uint64 r_scause()
{
	uint64 x;
	asm volatile("csrr %0, scause" : "=r" (x) );
	return x;
}

// Supervisor Trap Value
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

// machine exception program counter, holds the
// instruction address to which a return from
// exception will go.
// sepc 的全称是 Supervisor Exception Program Counter
// 当发生异常或中断（Trap）时，保存被中断程序的下一条指令的地址（即程序计数器 PC 的值），
// 以便处理完毕后能够准确返回
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
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4) // 1 -> user can access

/**
 * 在 RISC-V 的 Sv39 方案中，一个 64 位的 PTE 内部结构如下：
 * - 高 44 位（位 53~10）：物理页号（PPN, Physical Page Number）
 * - 低 10 位（位 9~0）：标志位（Flags），包含有效位（V）、读写执行权限（R/W/X）等
 */
// shift a physical address to the right place for a PTE.
#define PA2PTE(pa) ((((uint64)pa) >> 12) << 10)
// (pte) >> 10, 清除低 10 位的标志位（Flags）
// << 12, 将物理页号还原为真实的物理内存基地址
// 这意味着任何一个物理页的起始地址，其最低的 12 位必然全是 0（即 4KB 对齐）
#define PTE2PA(pte) (((pte) >> 10) << 12)
// 10 位标志位
#define PTE_FLAGS(pte) ((pte) & 0x3FF)

// extract the three 9-bit page table indices from a virtual address.
#define PXMASK          0x1FF // 9 bits
#define PXSHIFT(level)  (PGSHIFT+(9*(level)))
#define PX(level, va) ((((uint64) (va)) >> PXSHIFT(level)) & PXMASK)

// one beyond the highest possible virtual address.
// MAXVA is actually one bit less than the max allowed by
// Sv39, to avoid having to sign-extend virtual addresses
// that have the high bit set.
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))

typedef uint64 pte_t;
typedef uint64 *pagetable_t; // 512 PTEs

#endif /** __riscv_h__ */
