#include "defs.h"

void main()
{
	if (cpuid() == 0) {
		consoleinit();
		printfinit();
		printf("\n");
		printf("Tiny-OS kernel is booting\n");
		kinit();         // physical page allocator
		kvminit();       // create kernel page table
		kvminithart();   // turn on paging
		procinit();      // process table
		printf("init finished\n");
	}
}