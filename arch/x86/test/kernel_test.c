#include "../kernel/defs.h"
#include "../kernel/x86.h"
#include "../kernel/interrupt.h"

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
}
