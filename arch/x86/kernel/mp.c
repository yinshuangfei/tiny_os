/*
 * SMP：启动 Application Processors（对齐 Linux smpboot 骨架）。
 * 流程：trampoline → start_secondary → scheduler。
 * QEMU：APIC ID 与逻辑 CPU 号一致（0..n-1）。
 * 共享全局就绪队列，每核跑 scheduler() + 本核 LAPIC timer。
 */
#include "types.h"
#include "defs.h"
#include "param.h"
#include "proc.h"
#include "mp.h"
#include "lapic.h"
#include "gdt.h"
#include "idt.h"
#include "x86.h"
#include "mm/vm.h"
#include "mm/memlayout.h"

#define TRAMPOLINE	0x8000u
#define FW_CFG_PORT_SEL	0x510
#define FW_CFG_PORT_DATA 0x511
#define FW_CFG_SIGNATURE 0x0000
#define FW_CFG_NB_CPUS	0x0005

int ncpu = 1;				/* num_online_cpus，bring-up 后更新 */
volatile int ap_started[NR_CPUS];

extern char trampoline[], etrampoline[];
extern char kernel_stacks[NR_CPUS][KSTACKSIZE];
extern pagetable_t cpu_user_pgdir[NR_CPUS];

static inline uint8 fw_cfg_read8(void)
{
	return inb(FW_CFG_PORT_DATA);
}

static inline void fw_cfg_select(uint16 key)
{
	outw(key, FW_CFG_PORT_SEL);
}

static int fw_cfg_cpu_count(uint *count)
{
	uint32 sig;
	uint n;

	if (!count)
		return -1;

	fw_cfg_select(FW_CFG_SIGNATURE);
	sig = fw_cfg_read8();
	sig |= (uint32)fw_cfg_read8() << 8;
	sig |= (uint32)fw_cfg_read8() << 16;
	sig |= (uint32)fw_cfg_read8() << 24;
	if (sig != 0x554d4551)
		return -1;

	fw_cfg_select(FW_CFG_NB_CPUS);
	n = (uint)fw_cfg_read8();
	n |= (uint)fw_cfg_read8() << 8;
	if (n == 0)
		return -1;

	*count = n;
	return 0;
}

static uint cpuid_cpu_count(void)
{
	uint32 eax, ebx, ecx, edx;
	uint n;

	if (!cpu_has_cpuid())
		return 1;
	cpuid(0, &eax, &ebx, &ecx, &edx);
	if (eax < 1)
		return 1;
	cpuid(1, &eax, &ebx, &ecx, &edx);
	n = (ebx >> 16) & 0xff;
	return n ? n : 1;
}

static int mp_present_cpus(void)
{
	uint count;

	if (fw_cfg_cpu_count(&count) == 0)
		return (int)count;
	return (int)cpuid_cpu_count();
}

int cpu_id(void)
{
	uint32 aid;
	int i;

	if (!lapic)
		return 0;
	aid = lapic_id();
	for (i = 0; i < ncpu; i++) {
		if ((uint32)cpus[i].apicid == aid)
			return i;
	}
	/* 启动早期 AP 尚未计入 ncpu 时：APIC id 即下标 */
	if (aid < (uint32)NR_CPUS)
		return (int)aid;
	return 0;
}

/*
 * Linux do_boot_cpu：写 trampoline 启动参数并发 INIT-SIPI-SIPI。
 * 参数布局见 trampoline.S（TRAMPOLINE-4/-8/-12）。
 */
static void do_boot_cpu(int cpu, uint32 apicid)
{
	uint32 *p = (uint32 *)TRAMPOLINE;
	int i;

	/* 0x8000-4: 栈顶，0x8000-8: 入口，0x8000-12: 内核页表 */
	p[-1] = (uint32)&kernel_stacks[cpu][0] + KSTACKSIZE;
	p[-2] = (uint32)start_secondary;
	p[-3] = V2P((uint32)kernel_pgdir);

	ap_started[cpu] = 0;
	cpus[cpu].id = cpu;
	cpus[cpu].apicid = (int)apicid;

	lapic_startap(apicid, TRAMPOLINE);

	for (i = 0; i < 1000; i++) {
		if (ap_started[cpu])
			return;
		lapic_microdelay(1000);
	}
	printk(KERN_ERR "mp: cpu%d (apicid=%u) start timeout\n", cpu, apicid);
}

void mp_init(void)
{
	int i;
	int present;
	int target;
	uint len;

	cpus[0].id = 0;
	cpus[0].apicid = (int)lapic_id();
	ncpu = 1;
	ap_started[0] = 1;

	present = mp_present_cpus();
	if (present < 1)
		present = 1;
	target = present;
	if (target > SETUP_MAX_CPUS)
		target = SETUP_MAX_CPUS;
	if (target > NR_CPUS)
		target = NR_CPUS;
	if (target <= 1) {
		printk(KERN_INFO
		       "mp: runtime reports %d CPU(s), booting uniprocessor (maxcpus=%d, nr_cpus=%d)\n",
		       present, SETUP_MAX_CPUS, NR_CPUS);
		return;
	}

	/* setup_trampoline：拷到低地址一次即可（立即数已按 0x8000 编码） */
	len = (uint)(etrampoline - trampoline);
	if (len == 0 || len > PGSIZE)
		panic("mp: bad trampoline");
	memcpy((void *)TRAMPOLINE, trampoline, len);

	printk(KERN_INFO
	       "mp: runtime reports %d CPU(s), booting %d (maxcpus=%d, nr_cpus=%d)\n",
	       present, target, SETUP_MAX_CPUS, NR_CPUS);

	for (i = 1; i < target; i++) {
		/*
		 * QEMU：顺序 APIC ID = 0,1,2,...
		 * 先增大 ncpu，便于 AP 的 cpu_id() 解析。
		 */
		ncpu = i + 1;
		do_boot_cpu(i, (uint32)i);
		if (!ap_started[i]) {
			ncpu = i;
			break;
		}
	}

	printk(KERN_INFO "mp: %d CPUs online\n", num_online_cpus());
}

/*
 * Linux start_secondary：AP 进入点。
 * 页表已开、栈已设；配置本核 GDT/TSS/IDT/LAPIC 后进入调度。
 * 勿在此前做多余事（SMP bring-up 很脆）。
 */
void start_secondary(void)
{
	int id;

	id = cpu_id();
	cpus[id].id = id;
	cpus[id].apicid = (int)lapic_id();
	cpus[id].proc = 0;
	cpus[id].noff = 0;
	cpus[id].intena = 0;
	cpus[id].need_resched = 0;
	cpu_user_pgdir[id] = 0;

	/* 重载本核 GDT/TSS（与 BSP 同一张表，ltr 不同选择子） */
	gdt_load_ap(id);
	/* IDTR 亦为 per-CPU，须对本核 lidt */
	idt_load_ap();
	cpu_init_ap();

	lapic_init();
	lapic_timer_init_ap();

	__sync_synchronize();
	ap_started[id] = 1;

	printk(KERN_INFO "mp: cpu%d online (apicid=%d)\n",
	       id, cpus[id].apicid);

	sti();
	scheduler();
}
