/*
 * ATA PIO 轮询驱动（教学用，无 IRQ）。
 *
 * 主通道 master：数据口 0x1F0，命令/状态 0x1F7，设备控制 0x3F6。
 * 命令：IDENTIFY(0xEC)、READ SECTORS(0x20)、WRITE SECTORS(0x30)。
 */
#include "ide.h"
#include "defs.h"
#include "x86.h"
#include "spinlock.h"
#include "printk.h"

/* 主 IDE 通道端口 */
#define IDE_DATA	0x1f0	/* 数据寄存器 */
#define IDE_ERROR	0x1f1	/* 错误寄存器 */
#define IDE_SECCOUNT	0x1f2	/* 扇区计数寄存器 */
#define IDE_LBA0	0x1f3	/* 扇区号低 8 位 */
#define IDE_LBA1	0x1f4	/* 扇区号中 8 位 */
#define IDE_LBA2	0x1f5	/* 扇区号高 8 位 */
#define IDE_DRIVE	0x1f6	/* 驱动器寄存器 */
#define IDE_CMD		0x1f7	/* 命令寄存器 */
#define IDE_STATUS	0x1f7	/* 状态寄存器 */
#define IDE_ALTSTATUS	0x3f6	/* 备用状态寄存器 */

/* 状态位 */
#define IDE_BSY		0x80	/* 忙 */
#define IDE_DRDY	0x40	/* 设备就绪 */
#define IDE_DF		0x20	/* 设备故障 */
#define IDE_DRQ		0x08	/* 数据请求 */
#define IDE_ERR		0x01	/* 错误 */

/* 命令 */
#define IDE_CMD_READ	0x20	/* 读命令 */
#define IDE_CMD_WRITE	0x30	/* 写命令 */
#define IDE_CMD_IDENTIFY 0xec	/* IDENTIFY 命令 */

/* DRIVE 寄存器：LBA、master */
#define IDE_SEL_LBA_MASTER	0xe0	/* LBA、master */

static struct spinlock ide_lock;
static int ide_present;		/* 探测到磁盘 */
static uint ide_sectors;	/* 总扇区数（LBA28） */
static char ide_model[41];	/* IDENTIFY 型号字符串 */

static void ide_delay(void)
{
	int i;

	/* 读状态口约 400ns，满足命令后最短等待 */
	for (i = 0; i < 4; i++) {
		(void)inb(IDE_ALTSTATUS);
	}
}

/* 等待 !(status & mask) 为真，或超时。返回最后读到的 status，超时 -1 */
static int ide_wait(int mask, int check_err)
{
	int i;
	int r;

	for (i = 0; i < 100000; i++) {
		r = inb(IDE_STATUS);
		if ((r & IDE_BSY) == 0 && (r & mask) == mask) {
			if (check_err && (r & (IDE_DF | IDE_ERR)) != 0) {
				return -1;
			}
			return r;
		}
	}
	return -1;
}

/*
 * 发出读写/IDENTIFY 前的 LBA28 寻址：扇区数=1，驱动器=master。
 * cmd 为 ATA 命令字节。
 */
static void ide_start(uint lba, uchar cmd)
{
	ide_delay();
	outb(IDE_SECCOUNT, 1);
	outb(IDE_LBA0, lba & 0xff);
	outb(IDE_LBA1, (lba >> 8) & 0xff);
	outb(IDE_LBA2, (lba >> 16) & 0xff);
	outb(IDE_DRIVE, IDE_SEL_LBA_MASTER | ((lba >> 24) & 0x0f));
	outb(IDE_CMD, cmd);
}

/* 从 IDE_DATA 寄存器读取数据 */
static void ide_insw(void *buf, int words)
{
	ushort *p = buf;
	int i;

	for (i = 0; i < words; i++) {
		p[i] = inw(IDE_DATA);
	}
}

/* 写入数据到 IDE_DATA 寄存器 */
static void ide_outsw(const void *buf, int words)
{
	const ushort *p = buf;
	int i;

	for (i = 0; i < words; i++) {
		outw(p[i], IDE_DATA);
	}
}

uint ide_nsectors(void)
{
	return ide_sectors;
}

/* 从磁盘读取数据 */
int ide_read(uint lba, void *buf)
{
	int r;

	if (!ide_present || !buf) {
		return -1;
	}
	if (ide_sectors && lba >= ide_sectors) {
		return -1;
	}

	acquire(&ide_lock);
	ide_start(lba, IDE_CMD_READ);
	r = ide_wait(IDE_DRQ, 1);
	if (r < 0) {
		release(&ide_lock);
		return -1;
	}
	ide_insw(buf, SECTSIZE / 2);
	release(&ide_lock);
	return 0;
}

/* 写入数据到磁盘 */
int ide_write(uint lba, const void *buf)
{
	int r;

	if (!ide_present || !buf) {
		return -1;
	}
	if (ide_sectors && lba >= ide_sectors) {
		return -1;
	}

	acquire(&ide_lock);
	ide_start(lba, IDE_CMD_WRITE);
	r = ide_wait(IDE_DRQ, 1);
	if (r < 0) {
		release(&ide_lock);
		printk(KERN_ERR "ide: write wait failed, r = %d\n", r);
		return -1;
	}
	ide_outsw(buf, SECTSIZE / 2);
	/* 写完等 BSY 清、设备就绪 */
	r = ide_wait(IDE_DRDY, 1);
	release(&ide_lock);
	return r < 0 ? -1 : 0;
}

/*
 * IDENTIFY：读 256 字；型号在字 27–46，扇区数在字 60–61（小端）。
 */
static int ide_identify(void)
{
	ushort id[256];
	int i, r;
	char *p;

	outb(IDE_DRIVE, IDE_SEL_LBA_MASTER);
	ide_delay();
	outb(IDE_SECCOUNT, 0);
	outb(IDE_LBA0, 0);
	outb(IDE_LBA1, 0);
	outb(IDE_LBA2, 0);
	outb(IDE_CMD, IDE_CMD_IDENTIFY);
	ide_delay();

	r = inb(IDE_STATUS);
	if (r == 0 || r == 0xff) {
		return -1;	/* 无设备 */
	}

	if (ide_wait(IDE_DRQ, 1) < 0) {
		return -1;
	}
	ide_insw(id, 256);

	/* 型号：字内字节需交换 */
	p = ide_model;
	for (i = 0; i < 20; i++) {
		p[i * 2] = (char)(id[27 + i] >> 8);
		p[i * 2 + 1] = (char)(id[27 + i] & 0xff);
	}
	p[40] = '\0';
	/* 去尾空格 */
	for (i = 39; i >= 0 && (ide_model[i] == ' ' || ide_model[i] == '\0'); i--) {
		ide_model[i] = '\0';
	}

	ide_sectors = id[60] | ((uint)id[61] << 16);
	return 0;
}

/*
 * 非破坏自检：只读 LBA0。
 * 切勿在 init 里写低号扇区——VMware/真机上 primary master 常是启动盘，
 * 写 LBA1 会毁掉引导/内核，表现为「串口再也没有输出」。
 */
static void ide_smoke_read(void)
{
	uchar rbuf[SECTSIZE];

	if (ide_read(0, rbuf) < 0) {
		printk(KERN_ERR "ide: smoke read LBA0 failed\n");
		return;
	}
	printk(KERN_INFO "ide: smoke read LBA0 ok\n");
}

void ide_init(void)
{
	int st;

	initlock(&ide_lock, "ide");
	ide_present = 0;
	ide_sectors = 0;
	ide_model[0] = '\0';

	/*
	 * 选 master 后读状态：0x00/0xFF 通常表示该通道无盘。
	 * QEMU：需 piix3-ide + ide-hd；VMware：若启动盘是 IDE 则会探测到它。
	 */
	outb(IDE_DRIVE, IDE_SEL_LBA_MASTER);
	ide_delay();
	st = inb(IDE_STATUS) & 0xff;
	if (st == 0xff || st == 0x00) {
		printk(KERN_WARNING "ide: no disk on primary master (status=0x%x)\n",
		       st);
		return;
	}

	if (ide_identify() < 0) {
		printk(KERN_WARNING "ide: IDENTIFY failed, no ide device found\n");
		return;
	}

	ide_present = 1;
	printk(KERN_INFO "ide: model (%s), %d sectors (%d KiB)\n",
	       ide_model[0] ? ide_model : "(unknown)",
	       ide_sectors,
	       ide_sectors / 2);

	ide_smoke_read();
}
