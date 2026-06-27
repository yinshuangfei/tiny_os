#include "defs.h"

volatile static int started = 0;

void one_loop_while_intr()
{
	printf("status:0x%x\n", r_sstatus());
	intr_on();
	while (1) ;
	printf("never get here\n");
}

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
		trapinit();      // trap vectors
		trapinithart();  // install kernel trap vector
		plicinit();      // set up interrupt controller
		plicinithart();  // ask PLIC for device interrupts
		/** plicinithart() 设置后，中断开始起作用 */

		binit();         // buffer cache
		iinit();         // inode cache
		fileinit();      // file table
		// virtio_disk_init(); // emulated hard disk
		userinit();      // first user process
		__sync_synchronize();
		started = 1;
		printf("Tiny-OS kernel init finished 😃\n");
		dump_pagetable();
	} else {
		while (started == 0)
			;
		__sync_synchronize();
		printf("hart %d starting\n", cpuid());
		kvminithart();    // turn on paging
		trapinithart();   // install kernel trap vector
		plicinithart();   // ask PLIC for device interrupts
	}

	scheduler();
}