#include "defs.h"
#include "debug.h"
#include "x86.h"

#define MAJOR_VERSION 0
#define MINOR_VERSION 0
#define PATCH_VERSION 1

void kernel_test(void);

void main(void)
{
	// 初始化串口
	serial_init();
	printf("\n\rTiny-OS (%d.%d.%d) booting ...\n\r",
		MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION);

	// 初始化 GDT
	gdt_init();
	// 初始化 IDT
	idt_init();
	// 检测并开启 SSE（须在可能生成 SSE 指令的代码之前）
	cpu_init();
	// 探测物理内存大小（须在 pmm_init / kvm_init 之前）
	mem_init();
	// 初始化 PIC/APIC
	pic_init();
	// 初始化物理页分配器
	pmm_init();
	// 内核小对象堆（PCB 等动态结构）
	kmem_init();
	// 初始化页表并开启分页
	kvm_init();
	// 初始化进程表
	procinit();
	// printf 自旋锁（须在 sti 之前，避免定时器 IRQ 交错输出）
	printfinit();
	// 初始化时钟
	pit_init();
	// 开启中断
	sti();

	kernel_test();
	scheduler();
}
