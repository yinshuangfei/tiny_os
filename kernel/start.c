#include "param.h"
#include "defs.h"

// entry.S needs one stack per CPU.
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

void test_printf()
{
	printf("hello1\n");
	printf("%s\n", "hello2");
	printf("%s\n", "");
	printf("%s\n", 0);
	printf("%d\n", 100);
	printf("%x\n", 16);
	printf("%o\n", 8);
	printf("%p\n", 0x8fffffff);
	printf("%p\n", 0x8fffffffL);
}

void start()
{
	printf("xv6 kernel is booting\n");
	test_printf();

	// switch to supervisor mode and jump to main().
	// asm volatile("mret");
}