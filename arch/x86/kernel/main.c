#include "defs.h"
#include "debug.h"
#include "gdt.h"
#include "idt.h"
#include "x86.h"

#define MAJOR_VERSION 0
#define MINOR_VERSION 0
#define PATCH_VERSION 1

void main(void)
{
	/* 初始化串口 */
	serial_init();
	printk(KERN_INFO "Tiny-OS (%d.%d.%d) booting ...\n",
		MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION);
	/* 初始化 GDT */
	gdt_init();
	/* 初始化 IDT */
	idt_init();
	/* 检测并开启 SSE（须在可能生成 SSE 指令的代码之前） */
	cpu_init();
	/* 探测物理内存大小（须在 pmm_init / kvm_init 之前） */
	mem_probe();
	/* 初始化 PIC/APIC */
	pic_init();
	/* 初始化物理页分配器 */
	pmm_init();
	/* 内核小对象堆（PCB 等动态结构） */
	kmem_init();
	/* 初始化页表并开启分页 */
	kvm_init();
	/* 初始化进程表 */
	procinit();
	/* 内存文件系统 */
	fs_init();
	/* 块设备层（须在 IDE 等驱动注册 gendisk 之前） */
	blk_init();
	/* IDE 设备初始化（探测后注册 hda/hdb/…） */
	ide_init();
	/* printf 自旋锁（须在 sti 之前，避免定时器 IRQ 交错输出） */
	printfinit();
	/* 初始化时钟（尚未 sti，不会抢占） */
	pit_init();
	/*
	 * 先创建 init/kthreadd，再开中断。
	 * 若先 sti，定时器可能在 rest_init 中途打断；虽然 swapper 无 kstack
	 * 通常不会被 preempt，但 boot 阶段持锁/分配时开中断仍不安全，
	 * 且曾出现偶发「init 创建成功、kthreadd 分配失败」。
	 */
	rest_init();
	sti();
	/* 启动内核线程调度器 */
	scheduler();
}
