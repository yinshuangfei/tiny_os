#include "../kernel/defs.h"
#include "../kernel/x86.h"
#include "../kernel/interrupt.h"
#include "../kernel/memlayout.h"
#include "../kernel/timer.h"
#include "../kernel/debug.h"

void paging_enable_test(void)
{
	int *bad_ptr = (int *)0xDEADBEEF;

	/** 禁用分页 */
	w_cr0(r_cr0() & ~CR0_PG);

	/* 故意访问未映射的地址 */
	printf("Not triggering page fault...\n");
	*bad_ptr = 42;

	/** 启用分页 */
	w_cr0(r_cr0() | CR0_PG);

	/* 故意访问未映射的地址 */
	printf("Triggering page fault...\n");
	*bad_ptr = 42;
}

void divide_error_test(void)
{
	int a = 1, b = 0;
	printf("Triggering divide error... \n");
	a = a / b;
}

void general_protection_test(void)
{
	printf("Triggering general protection (#GP)...\n");
	/*
	 * 向 DS 加载超出 GDT 范围的选择子（index 8191）触发 #GP。
	 * 比 null selector 更直观：明确是“描述符索引非法”。
	 */
	__asm__ volatile (
		"cli\n\t"
		"movw $0xffff, %%ax\n\t"
		"movw %%ax, %%ds"
		::: "ax", "memory");
}

void device_not_available_test(void)
{
	printf("Triggering device not available (#NM)...\n");
	/*
	 * CR0.TS=1 时执行 SSE/FP 指令会触发 #NM；handler 清 TS 后返回重试。
	 */
	w_cr0(r_cr0() | CR0_TS);
	__asm__ volatile ("xorps %%xmm0, %%xmm0" ::: "xmm0", "memory");
	printf("#NM handled, SSE instruction completed\n");
}

void simd_fp_test(void)
{
	static const float one = 1.0f;
	uint32 mxcsr;

	printf("Triggering simd fp exception (#XM)...\n");
	/*
	 * 取消 MXCSR 除零屏蔽（bit 9），再执行 1.0/0.0 的 divss 触发 #XM。
	 * 真机上 divss 直接 #XM；QEMU 可能不投递，fall-through 到 int $0x13
	 * 进入同一 vector（真机不会执行到 int）。
	 */
	__asm__ volatile (
		"stmxcsr %0\n\t"
		"andl $0xfffffdff, %0\n\t"
		"ldmxcsr %0\n\t"
		"xorps %%xmm1, %%xmm1\n\t"
		"movss %1, %%xmm0\n\t"
		"divss %%xmm1, %%xmm0\n\t"
		"int $0x13"
		: "+m"(mxcsr)
		: "m"(one)
		: "xmm0", "xmm1", "memory");
}

static void sleep_ticks_kthread(void *arg)
{
	unsigned int t0, t1, delta, to_sleep = 10;

	(void)arg;
	printf("sleep_ticks test: %d\n", to_sleep);

	t0 = timer_ticks();
	sleep_ticks(to_sleep);
	t1 = timer_ticks();
	delta = t1 - t0;

	printf("sleep_ticks test: %d -> %d (delta=%d, expect ~%d)\n",
	       (int)t0, (int)t1, (int)delta, to_sleep);
	if (delta < to_sleep / 2 || delta > to_sleep * 2)
		panic("sleep_ticks test --- FAILED");
	printf("sleep_ticks test --- PASSED\n");
}

void sleep_ticks_test(void)
{
	if (!kthread_create(sleep_ticks_kthread, 0, "sleep-test"))
		panic("sleep_ticks_test: kthread_create failed");
}

static void kthread_a(void *arg)
{
	int i;

	(void)arg;
	for (i = 0; i < 10; i++) {
		printf("  kthread A: round %d\n", i);
		yield();
	}
}

static void kthread_b(void *arg)
{
	int i;

	(void)arg;
	for (i = 0; i < 10; i++) {
		printf("  kthread B: round %d\n", i);
		yield();
	}
}

static void kthread_spinner(void *arg)
{
	int id = (int)arg;
	int target = 4;

	for (int i = 0; i < target; i++) {
		/*
		 * 忙等，不 yield；依赖定时器抢占。
		 * for 循环中单次执行时间小于一个 tick，不会触发抢占。
		 */
		for (volatile int j = 0; j < 8000000; j++)
			;
		printf("  spinner %d: slice [%d/%d] done\n", id, i+1, target);
	}
}

void kthread_preempt_test(void)
{
	printf("kthread preempt test (no yield, one tick one switch):\n");
	if (!kthread_create(kthread_spinner, (void *)1, "spin-1"))
		panic("kthread_create spin-1 failed");
	if (!kthread_create(kthread_spinner, (void *)2, "spin-2"))
		panic("kthread_create spin-2 failed");
}

void kthread_yield_test(void)
{
	printf("kthread switch test:\n");
	if (!kthread_create(kthread_a, 0, "kthread-a"))
		panic("kthread_create A failed");
	if (!kthread_create(kthread_b, 0, "kthread-b"))
		panic("kthread_create B failed");
	printf("kthread test: threads created, hand over to scheduler\n");
}

static int test_check(const char *name, int ok)
{
	printf("  %s: %s\n", name, ok ? "OK" : "FAIL");
	return ok;
}

void gdt_test(void)
{
	struct gdt_ptr gdtr;
	int pass = 1;

	printf("GDT test:\n");

	/* 1. 段寄存器应指向内核代码/数据段 */
	pass &= test_check("CS == SEG_KCODE", r_cs() == SEG_KCODE);
	pass &= test_check("DS == SEG_KDATA", r_ds() == SEG_KDATA);
	pass &= test_check("ES == SEG_KDATA", r_es() == SEG_KDATA);
	pass &= test_check("SS == SEG_KDATA", r_ss() == SEG_KDATA);

	/* 2. GDTR 应指向内核 GDT 表 */
	r_gdtr(&gdtr);
	printf("  GDTR: base=0x%x limit=0x%x\n", gdtr.base, gdtr.limit);
	pass &= test_check("GDTR.base == gdt", gdtr.base == gdt_table_addr());
	pass &= test_check("GDTR.limit", gdtr.limit == gdt_table_limit());

	/* 3. GDT 表项 access / granularity（access 低 bit 可能已被 CPU 置 Accessed） */
	pass &= test_check("gdt[1] kernel code",
			   (gdt_get_access(1) & GDT_ACCESS_MASK) == GDT_KERNEL_CODE &&
			   gdt_get_granularity(1) == GDT_GRAN_4GB);
	pass &= test_check("gdt[2] kernel data",
			   (gdt_get_access(2) & GDT_ACCESS_MASK) == GDT_KERNEL_DATA &&
			   gdt_get_granularity(2) == GDT_GRAN_4GB);
	pass &= test_check("gdt[3] user code",
			   (gdt_get_access(3) & GDT_ACCESS_MASK) == GDT_USER_CODE &&
			   gdt_get_granularity(3) == GDT_GRAN_4GB);
	pass &= test_check("gdt[4] user data",
			   (gdt_get_access(4) & GDT_ACCESS_MASK) == GDT_USER_DATA &&
			   gdt_get_granularity(4) == GDT_GRAN_4GB);
	pass &= test_check("gdt[5] TSS",
			   (gdt_get_access(5) & GDT_ACCESS_MASK) ==
			   (GDT_TSS & GDT_ACCESS_MASK));
	pass &= test_check("TR == SEG_TSS", r_tr() == SEG_TSS);
	pass &= test_check("TSS.ss0 == SEG_KDATA", tss_get_ss0() == SEG_KDATA);
	pass &= test_check("TSS.esp0 set",
			   tss_get_esp0() == (uint32)INTERRUPT_STACK_TOP ||
			   tss_get_esp0() > 0);

	if (!pass)
		panic("GDT test failed");
	printf("GDT test passed\n");
}

static int idt_gate_check(const char *tag, int vec, void (*handler)(void),
			  uint16 selector, uint8 type_attr)
{
	int ok = 1;

	printf("  idt[0x%x] %s handler: ", vec, tag);
	ok = idt_get_handler(vec) == (uint32)handler;
	printf("%s\n", ok ? "OK" : "FAIL");

	printf("  idt[0x%x] %s selector: ", vec, tag);
	if (idt_get_selector(vec) != selector) {
		printf("FAIL\n");
		ok = 0;
	} else {
		printf("OK\n");
	}

	printf("  idt[0x%x] %s attr: ", vec, tag);
	if (idt_get_type_attr(vec) != type_attr) {
		printf("FAIL\n");
		ok = 0;
	} else {
		printf("OK\n");
	}

	return ok;
}

void idt_test(void)
{
	struct idt_ptr idtr;
	int pass = 1;

	extern void isr_divide_error(void);
	extern void isr_device_not_available(void);
	extern void isr_general_protection(void);
	extern void isr_page_fault(void);
	extern void isr_simd_fp(void);
	extern void isr_timer(void);
	extern void isr_syscall(void);

	printf("IDT test:\n");

	/* 1. IDTR 应指向内核 IDT 表 */
	r_idtr(&idtr);
	printf("  IDTR: base=0x%x limit=0x%x\n", idtr.base, idtr.limit);
	pass &= test_check("IDTR.base == idt", idtr.base == idt_table_addr());
	pass &= test_check("IDTR.limit", idtr.limit == idt_table_limit());

	/* 2. 已注册的中断/异常门 */
	pass &= idt_gate_check("divide", EXC_DIVIDE_ERROR, isr_divide_error,
			       SEG_KCODE, IDT_ATTR_KERNEL);
	pass &= idt_gate_check("#NM", EXC_DEVICE_NOT_AVAILABLE, isr_device_not_available,
			       SEG_KCODE, IDT_ATTR_KERNEL);
	pass &= idt_gate_check("page fault", EXC_PAGE_FAULT, isr_page_fault,
			       SEG_KCODE, IDT_ATTR_KERNEL);
	pass &= idt_gate_check("#GP", EXC_GENERAL_PROTECTION, isr_general_protection,
			       SEG_KCODE, IDT_ATTR_KERNEL);
	pass &= idt_gate_check("#XM", EXC_SIMD_FP, isr_simd_fp,
			       SEG_KCODE, IDT_ATTR_KERNEL);
	pass &= idt_gate_check("timer", IRQ_TIMER, isr_timer,
			       SEG_KCODE, IDT_ATTR_KERNEL);
	pass &= idt_gate_check("syscall", INT_SYSCALL, isr_syscall,
			       SEG_KCODE, IDT_ATTR_USER);

	if (!pass)
		panic("IDT test failed");
	printf("IDT test passed\n");
}

void kernel_test(void)
{
	// gdt_test();
	// idt_test();

	// paging_enable_test();
	// divide_error_test();
	// general_protection_test();
	// device_not_available_test();
	// simd_fp_test();
	// sleep_ticks_test();
	// kthread_yield_test();
	// kthread_preempt_test();
	dump_proc_table();
	dump_runqueue();
}