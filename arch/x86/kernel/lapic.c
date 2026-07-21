/*
 * Local APIC 驱动（xAPIC）：MMIO、软件使能、EOI、周期定时器。
 *
 * 时钟：用 LAPIC timer 打向量 0x20（与原 PIT/IRQ0 相同），
 * 因 QEMU q35 上 PIT→IOAPIC GSI0 常不可靠，nanosleep 会挂死。
 */
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "lapic.h"
#include "idt.h"
#include "mm/mmu.h"
#include "mm/vm.h"
#include "mm/memlayout.h"

/* 寄存器偏移（32 位宽，地址对齐 16 字节） */
#define LAPIC_ID	0x0020	/* Processor ID Register */
#define LAPIC_VER	0x0030	/* Version Register */
#define LAPIC_TPR	0x0080	/* Task Priority Register */
#define LAPIC_EOI	0x00B0	/* End of Interrupt Register */
#define LAPIC_SVR	0x00F0	/* Software Enable Register */
#define LAPIC_ESR	0x0280	/* Error Status Register */
#define LAPIC_ICRLO	0x0300	/* Interrupt Command Register Low */
#define LAPIC_ICRHI	0x0310	/* Interrupt Command Register High */
#define LAPIC_TIMER	0x0320	/* LVT Timer */
#define LAPIC_TICR	0x0380	/* Initial Count */
#define LAPIC_TCCR	0x0390	/* Current Count */
#define LAPIC_TDCR	0x03E0	/* Divide Configuration */

#define SVR_ENABLE	0x100	/* bit8：APIC 软件使能 */
#define SVR_VECTOR	0xff	/* 伪中断向量（须非 0，常用 0xFF） */

#define TIMER_PERIODIC	0x20000	/* LVT Timer：周期模式 */
#define TDCR_DIV1	0xb	/* 分频 /1 */

#define PIT_FREQ	1193182u	/* PIT 频率 */

/* 导出给 trap.S：按 LAPIC ID 选 per-CPU 中断栈 */
volatile uint32 *lapic;
static int lapic_ok;
static uint32 lapic_ticr;	/* 校准后的 Initial Count，AP 复用 */

static uint32 lapic_r(int index)
{
	return lapic[index / 4];
}

static void lapic_w(int index, uint32 value)
{
	lapic[index / 4] = value;
	/* 读 ID 作同步，确保写完成（部分硬件需要） */
	(void)lapic_r(LAPIC_ID);
}

int lapic_present(void)
{
	uint32 eax, ebx, ecx, edx;

	if (!cpu_has_cpuid())
		return 0;
	cpuid(0, &eax, &ebx, &ecx, &edx);
	if (eax < 1)
		return 0;
	cpuid(1, &eax, &ebx, &ecx, &edx);
	return (edx >> 9) & 1;
}

void lapic_map(void)
{
	unsigned long long base;	/* 64 位 */
	uint pa;

	if (!lapic_present())
		return;

	base = rdmsr(MSR_IA32_APIC_BASE);
	pa = (uint)(base & 0xfffff000u);
	if (pa == 0)
		pa = LAPIC_DEFAULT_BASE;

	if (!(base & APIC_BASE_ENABLE)) {
		base |= APIC_BASE_ENABLE;
		wrmsr(MSR_IA32_APIC_BASE, base);
	}

	kvmmap(pa, pa, PGSIZE, PTE_W | PTE_PCD | PTE_PWT);
	lapic = (volatile uint32 *)pa;
}

int lapic_init(void)
{
	uint32 ver;

	if (!lapic) {
		printk(KERN_ERR "lapic: not mapped\n");
		return -1;
	}

	lapic_w(LAPIC_SVR, SVR_ENABLE | SVR_VECTOR);
	lapic_w(LAPIC_TPR, 0);

	ver = lapic_r(LAPIC_VER);
	lapic_ok = 1;
	printk(KERN_INFO "lapic: enabled id=%u ver=0x%x at %p\n",
	       lapic_id(), ver & 0xff, lapic);
	return 0;
}

/*
 * 用 PIT 通道 2（喇叭定时器）忙等 ms 毫秒，不依赖中断。
 * 用于校准 LAPIC 计数频率。
 */
static void pit_busywait_ms(unsigned int ms)
{
	uint32 count;

	count = (PIT_FREQ * ms) / 1000u;
	if (count == 0)
		count = 1;
	if (count > 0xffff)
		count = 0xffff;

	/* 开通道 2 门控，关扬声器数据 */
	outb(0x61, (inb(0x61) & ~0x02) | 0x01);
	/* ch2、lo/hi、mode 0（计数结束 OUT 变高） */
	outb(0x43, 0xb0);
	outb(0x42, count & 0xff);
	outb(0x42, (count >> 8) & 0xff);

	while ((inb(0x61) & 0x20) == 0)
		;
}

/*
 * 校准并启动 LAPIC 周期定时器 → 向量 IRQ_TIMER(0x20)。
 * hz 一般为 TIMER_HZ(100)。
 */
void lapic_timer_init(unsigned int hz)
{
	uint32 delta, ticr;

	if (!lapic_ok || !lapic || hz == 0)
		return;

	lapic_w(LAPIC_TDCR, TDCR_DIV1);
	lapic_w(LAPIC_TICR, 0xffffffffu);
	pit_busywait_ms(10);
	delta = 0xffffffffu - lapic_r(LAPIC_TCCR);

	/*
	 * 10ms 内数了 delta；周期 1000/hz ms →
	 * ticr = delta * (1000/hz) / 10 = delta * 100 / hz
	 */
	ticr = (delta / 10u) * (1000u / hz);
	if (ticr == 0)
		ticr = delta ? delta : 1000000u;

	lapic_w(LAPIC_TICR, ticr);
	lapic_w(LAPIC_TIMER, TIMER_PERIODIC | (uint32)IRQ_TIMER);
	lapic_ticr = ticr;

	printk(KERN_INFO "lapic: timer %u Hz (ticr=%u, 10ms_delta=%u)\n",
	       hz, ticr, delta);
}

/* AP：复用 BSP 校准值，避免再占 PIT */
void lapic_timer_init_ap(void)
{
	if (!lapic_ok || !lapic || lapic_ticr == 0)
		return;
	lapic_w(LAPIC_TDCR, TDCR_DIV1);
	lapic_w(LAPIC_TICR, lapic_ticr);
	lapic_w(LAPIC_TIMER, TIMER_PERIODIC | (uint32)IRQ_TIMER);
}

void lapic_eoi(void)
{
	if (lapic_ok && lapic)
		lapic_w(LAPIC_EOI, 0);
}

uint32 lapic_id(void)
{
	if (!lapic)
		return 0;
	return lapic_r(LAPIC_ID) >> 24;
}

void lapic_microdelay(unsigned int us)
{
	/* 粗延时：PIT ch2 忙等，至少 1ms 精度时向上取整 */
	unsigned int ms;

	if (us == 0)
		return;
	ms = (us + 999) / 1000;
	if (ms == 0)
		ms = 1;
	pit_busywait_ms(ms);
}

/*
 * INIT-SIPI-SIPI 启动 AP（addr 须 4KiB 对齐，落在 <1MiB）。
 */
#define ICR_INIT	0x00000500
#define ICR_STARTUP	0x00000600
#define ICR_ASSERT	0x00004000
#define ICR_LEVEL	0x00008000
#define ICR_DELIVS	0x00001000

/* 按 Intel 规定的 INIT → SIPI → SIPI 把指定 AP 从 addr 拉起来 */
void lapic_startap(uint32 apicid, uint32 addr)
{
	int i;
	uint16 *wrv;

	if (!lapic)
		return;

	/*
	 * 可选：CMOS 关机码 + 热复位向量（部分真实硬件需要；QEMU 通常可省略）。
	 */
	outb(0x70, 0xf);	/* 关闭 NMI */
	outb(0x71, 0x0a);
	wrv = (uint16 *)0x467;		/* 0x40:0x67 */
	wrv[0] = 0;
	wrv[1] = addr >> 4;

	/* INIT */
	lapic_w(LAPIC_ICRHI, apicid << 24);
	lapic_w(LAPIC_ICRLO, ICR_INIT | ICR_LEVEL | ICR_ASSERT);
	while (lapic_r(LAPIC_ICRLO) & ICR_DELIVS)
		;
	lapic_microdelay(200);
	lapic_w(LAPIC_ICRLO, ICR_INIT | ICR_LEVEL);
	while (lapic_r(LAPIC_ICRLO) & ICR_DELIVS)
		;
	lapic_microdelay(10000);

	/* SIPI × 2，向量 = 页号, 真正告诉 AP 从哪开始跑 */
	for (i = 0; i < 2; i++) {
		lapic_w(LAPIC_ICRHI, apicid << 24);
		lapic_w(LAPIC_ICRLO, ICR_STARTUP | (addr >> 12));
		while (lapic_r(LAPIC_ICRLO) & ICR_DELIVS)
			;
		lapic_microdelay(200);
	}
}
