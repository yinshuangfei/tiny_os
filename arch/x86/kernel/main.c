#include "defs.h"
#include "debug.h"
#include "gdt.h"
#include "idt.h"
#include "x86.h"
#include "fs/mount.h"
#include "mp.h"
#include "mm/memlayout.h"

#define MAJOR_VERSION 0
#define MINOR_VERSION 0
#define PATCH_VERSION 1

void main(void)
{
	/* 初始化串口 */
	serial_init();
	/* VGA 文本模式清屏 */
	vga_init();
	/* 注册 tty0(VGA) + ttyS0(串口)；此后 printk 同时输出到两处 */
	console_init();
	/* 初始化陷阱栈 */
	trapstack_init();
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
	/* 初始化 PIC（无 APIC 时的回退）；随后分页并尝试 APIC */
	pic_init();
	/* PS/2 键盘（IRQ1 → console 输入） */
	kbd_init();
	/* 初始化物理页分配器 */
	pmm_init();
	/* 内核小对象堆（PCB 等动态结构） */
	kmem_init();
	/*
	 * 字符设备表（对齐 Linux fs/char_dev.c::chrdev_init）。
	 * 须在各驱动 register_chrdev 之前；早于 fs_init 创建 /dev 节点。
	 */
	chrdev_init();
	/* 设备模型：bus/device/driver + blkdev 名表（drivers/base） */
	driver_core_init();
	/* /dev/console → CONSOLE_MAJOR（须在 chrdev_init 之后） */
	console_register_device();
	/* 初始化页表并开启分页 */
	kvm_init();
	/* Local APIC + IOAPIC（失败则继续用 8259） */
	apic_init();
	/* 初始化进程表 */
	procinit();
	/* 内存文件系统 */
	fs_init();
	/* 块设备层（须在 IDE 等驱动注册 gendisk 之前） */
	blk_init();
	/* IDE 设备初始化（探测后注册 hda/hdb/…） */
	ide_init();
	/* 扫描块设备：读超级块魔数识别 FS 后按需加载并挂到 /mnt */
	mount_init();
	/* printf 自旋锁（须在 sti 之前，避免定时器 IRQ 交错输出） */
	printfinit();
	/*
	 * 时钟：有 APIC 时已由 lapic_timer_init 提供；
	 * 否则用 8254 PIT → PIC IRQ0。
	 */
	if (!irq_using_apic())
		pit_init();
	/*
	 * 先创建 init/kthreadd，再开中断。
	 */
	rest_init();
	/*
	 * 启动 AP（须在 apic、proc、printf 之后）。
	 * SETUP_MAX_CPUS 由 make CPUS= 传入，只表示 bring-up 上限；
	 * 实际 present CPU 数在 mp_init() 里运行时探测。
	 * AP 进入 scheduler 与 BSP 并行抢就绪队列。
	 */
	mp_init();
	sti();
	/* 启动内核线程调度器 */
	scheduler();
}
