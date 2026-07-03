#include "defs.h"
#include "debug.h"

void main(void)
{
	idt_init();

	kvminit();
	kvminithart();

	serial_init();
	timer_init();

	printf("Hello Tiny-OS\n");
	printf("paging enabled, cr3=0x%x\n", (unsigned int)kernel_pgdir);
}
