/*
 * ATA PIO 轮询驱动（教学用，无 IRQ）。
 *
 * 每个 IDE 控制器最多 2 通道 × master/slave。
 * 通道端口由板级描述表给出；扫描到磁盘后再 kmalloc 结构体。
 * 命令：IDENTIFY(0xEC)、READ SECTORS(0x20)、WRITE SECTORS(0x30)。
 */
#include "ide.h"
#include "defs.h"
#include "x86.h"
#include "spinlock.h"
#include "printk.h"
#include "block/blk.h"

/* 相对通道基址的寄存器偏移（主通道基址 0x1F0，次通道 0x170） */
#define IDE_DATA	0	/* 数据寄存器 */
#define IDE_ERROR	1	/* 错误寄存器 */
#define IDE_SECCOUNT	2	/* 扇区计数寄存器 */
#define IDE_LBA0	3	/* 扇区号低 8 位 */
#define IDE_LBA1	4	/* 扇区号中 8 位 */
#define IDE_LBA2	5	/* 扇区号高 8 位 */
#define IDE_DRIVE	6	/* 驱动器寄存器 */
#define IDE_CMD		7	/* 命令寄存器 */
#define IDE_STATUS	7	/* 状态寄存器 */

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

/* DRIVE 寄存器：LBA + master/slave（bit4） */
#define IDE_SEL_LBA(unit)	(0xe0 | (((unit) & 1) << 4))

#define IDE_NR_UNIT	2	/* 每通道 master/slave */

/* 一个通道的命令口 / 备用状态口 */
struct ide_chan_desc {
	ushort iobase;	/* 通道数据口基址 */
	ushort altport;	/* 备用状态口 */
};

/*
 * 板级 IDE 控制器描述（端口固定，磁盘结构体动态分配）。
 *
 * ide0：PCI piix3-ide，兼容主/次通道 0x1F0 / 0x170。
 * ide1：第二块控制器。QEMU 的第二个 piix3-ide 仍硬编码 0x1F0/0x170 会冲突，
 *       故用两路 isa-ide 模拟双通道扩展卡（经典第三/第四 IDE 口）。
 */
struct ide_ctl_desc {
	const char *name;		/* 控制器名称 */
	struct ide_chan_desc chan[2];	/* 通道描述 */
	int nchan;			/* 通道数 */
};

static const struct ide_ctl_desc ide_ctls[] = {
	{
		"ide0",
		{
			{ 0x1f0, 0x3f6 },	/* pri */
			{ 0x170, 0x376 },	/* sec */
		},
		2,
	},
	{
		"ide1",
		{
			{ 0x1e8, 0x3ee },	/* 第三 IDE 口 */
			{ 0x168, 0x36e },	/* 第四 IDE 口 */
		},
		2,
	},
};

struct ide_disk {
	ushort iobase;		/* 通道数据口基址 */
	ushort altport;		/* 备用状态口 */
	int unit;		/* 0=master, 1=slave */
	int drive;		/* 逻辑盘号（ide_tab 下标） */
	uint sectors;		/* 总扇区数（LBA28） */
	char model[41];		/* IDENTIFY 型号字符串 */
	char slot[24];		/* 如 ide1.0-master，仅日志 */
	struct gendisk *gd;	/* block 层整盘 */
};

static struct spinlock ide_lock;
static struct ide_disk **ide_tab;	/* 动态表：仅含已探测磁盘 */
static int ide_ndisk;			/* 已注册磁盘数 */

static struct ide_disk *ide_get(int drive)
{
	if (drive < 0 || drive >= ide_ndisk || !ide_tab)
		return 0;
	return ide_tab[drive];
}

static void ide_delay(struct ide_disk *d)
{
	int i;

	/* 读状态口约 400ns，满足命令后最短等待 */
	for (i = 0; i < 4; i++) {
		(void)inb(d->altport);
	}
}

/* 等待 !(status & mask) 为真，或超时。返回最后读到的 status，超时 -1 */
static int ide_wait(struct ide_disk *d, int mask, int check_err)
{
	int i;
	int r;

	for (i = 0; i < 100000; i++) {
		r = inb(d->iobase + IDE_STATUS);
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
 * 发出读写/IDENTIFY 前的 LBA28 寻址：扇区数=1，按磁盘选通道与 master/slave。
 * cmd 为 ATA 命令字节。
 */
static void ide_start(struct ide_disk *d, uint lba, uchar cmd)
{
	ide_delay(d);
	outb(d->iobase + IDE_SECCOUNT, 1);
	outb(d->iobase + IDE_LBA0, lba & 0xff);
	outb(d->iobase + IDE_LBA1, (lba >> 8) & 0xff);
	outb(d->iobase + IDE_LBA2, (lba >> 16) & 0xff);
	outb(d->iobase + IDE_DRIVE, IDE_SEL_LBA(d->unit) | ((lba >> 24) & 0x0f));
	outb(d->iobase + IDE_CMD, cmd);
}

/* 从 IDE_DATA 寄存器读取数据 */
static void ide_insw(struct ide_disk *d, void *buf, int words)
{
	ushort *p = buf;
	int i;

	for (i = 0; i < words; i++) {
		p[i] = inw(d->iobase + IDE_DATA);
	}
}

/* 写入数据到 IDE_DATA 寄存器 */
static void ide_outsw(struct ide_disk *d, const void *buf, int words)
{
	const ushort *p = buf;
	int i;

	for (i = 0; i < words; i++) {
		outw(p[i], d->iobase + IDE_DATA);
	}
}

int ide_ndisks(void)
{
	return ide_ndisk;
}

uint ide_nsectors(int drive)
{
	struct ide_disk *d = ide_get(drive);

	return d ? d->sectors : 0;
}

struct gendisk *ide_gendisk(int drive)
{
	struct ide_disk *d = ide_get(drive);

	return d ? d->gd : 0;
}

/*
 * Linux 风格 make_request_fn：处理 bio（可多扇区），同步 PIO 完成。
 * 由 submit_bio → d->gd->queue->make_request_fn 调用。
 */
static void ide_submit_bio(struct request_queue *q, struct bio *bio)
{
	struct ide_disk *d;
	sector_t sec;
	unsigned int left;
	char *p;
	unsigned int op;
	int r;

	d = q->queuedata;
	if (!d || !bio) {
		if (bio) {
			bio->bi_status = BLK_STS_IOERR;
			if (bio->bi_end_io)
				bio->bi_end_io(bio);
		}
		return;
	}

	sec = bio->bi_sector;
	left = bio->bi_size;
	p = bio->bi_data;
	op = bio->bi_opf & REQ_OP_MASK;
	bio->bi_status = BLK_STS_OK;

	while (left >= (unsigned int)SECTSIZE) {
		if (op == REQ_OP_WRITE)
			r = ide_write(d->drive, (uint)sec, p);
		else
			r = ide_read(d->drive, (uint)sec, p);
		if (r < 0) {
			bio->bi_status = BLK_STS_IOERR;
			break;
		}
		sec++;
		p += SECTSIZE;
		left -= SECTSIZE;
	}
	if (left != 0 && bio->bi_status == BLK_STS_OK)
		bio->bi_status = BLK_STS_IOERR;

	if (bio->bi_end_io)
		bio->bi_end_io(bio);
}

/* 将 ATA 盘注册为 gendisk（hda/hdb/…，major=IDE_MAJOR） */
static int ide_add_gendisk(struct ide_disk *d)
{
	struct gendisk *gd;
	char name[8];

	gd = alloc_disk(1);
	if (!gd)
		return -1;

	gd->major = IDE_MAJOR;
	gd->first_minor = d->drive;
	snprintf(name, sizeof(name), "hd%c", 'a' + (d->drive % 26));
	snprintf(gd->disk_name, sizeof(gd->disk_name), "%s", name);
	gd->private_data = d;
	set_capacity(gd, d->sectors);

	gd->queue->queuedata = d;
	blk_queue_logical_block_size(gd->queue, SECTSIZE);
	blk_queue_make_request(gd->queue, ide_submit_bio);

	d->gd = gd;
	add_disk(gd);
	return 0;
}

/* 从磁盘读取数据 */
int ide_read(int drive, uint lba, void *buf)
{
	struct ide_disk *d;
	int r;

	d = ide_get(drive);
	if (!d || !buf) {
		return -1;
	}
	if (d->sectors && lba >= d->sectors) {
		return -1;
	}

	acquire(&ide_lock);
	ide_start(d, lba, IDE_CMD_READ);
	r = ide_wait(d, IDE_DRQ, 1);
	if (r < 0) {
		release(&ide_lock);
		return -1;
	}
	ide_insw(d, buf, SECTSIZE / 2);
	release(&ide_lock);
	return 0;
}

/* 写入数据到磁盘 */
int ide_write(int drive, uint lba, const void *buf)
{
	struct ide_disk *d;
	int r;

	d = ide_get(drive);
	if (!d || !buf) {
		return -1;
	}
	if (d->sectors && lba >= d->sectors) {
		return -1;
	}

	acquire(&ide_lock);
	ide_start(d, lba, IDE_CMD_WRITE);
	r = ide_wait(d, IDE_DRQ, 1);
	if (r < 0) {
		release(&ide_lock);
		printk(KERN_ERR "ide: write drive=%d wait failed, r = %d\n",
		       drive, r);
		return -1;
	}
	ide_outsw(d, buf, SECTSIZE / 2);
	/* 写完等 BSY 清、设备就绪 */
	r = ide_wait(d, IDE_DRDY, 1);
	release(&ide_lock);
	return r < 0 ? -1 : 0;
}

/*
 * IDENTIFY：读 256 字；型号在字 27–46，扇区数在字 60–61（小端）。
 * 成功则填入 *d（调用方已设好 iobase/altport/unit）。
 */
static int ide_identify(struct ide_disk *d)
{
	ushort id[256];
	int i, r;
	char *p;

	outb(d->iobase + IDE_DRIVE, IDE_SEL_LBA(d->unit));
	ide_delay(d);
	outb(d->iobase + IDE_SECCOUNT, 0);
	outb(d->iobase + IDE_LBA0, 0);
	outb(d->iobase + IDE_LBA1, 0);
	outb(d->iobase + IDE_LBA2, 0);
	outb(d->iobase + IDE_CMD, IDE_CMD_IDENTIFY);
	ide_delay(d);

	r = inb(d->iobase + IDE_STATUS);
	if (r == 0 || r == 0xff) {
		return -1;	/* 无设备 */
	}

	if (ide_wait(d, IDE_DRQ, 1) < 0) {
		return -1;
	}
	ide_insw(d, id, 256);

	/* 型号：字内字节需交换 */
	p = d->model;
	for (i = 0; i < 20; i++) {
		p[i * 2] = (char)(id[27 + i] >> 8);
		p[i * 2 + 1] = (char)(id[27 + i] & 0xff);
	}
	p[40] = '\0';
	/* 去尾空格 */
	for (i = 39; i >= 0 && (d->model[i] == ' ' || d->model[i] == '\0'); i--) {
		d->model[i] = '\0';
	}

	d->sectors = id[60] | ((uint)id[61] << 16);
	return 0;
}

/*
 * 非破坏自检：只读 LBA0。
 * 切勿在 init 里写低号扇区——VMware/真机上 primary master 常是启动盘，
 * 写 LBA1 会毁掉引导/内核，表现为「串口再也没有输出」。
 */
/* 经 block 层读 LBA0，验证 gendisk → bio → ide_submit_bio 通路 */
static void ide_smoke_read(int drive)
{
	struct gendisk *gd;
	uchar rbuf[SECTSIZE];

	gd = ide_gendisk(drive);
	if (!gd) {
		printk(KERN_ERR "ide: smoke: no gendisk for drive=%d\n", drive);
		return;
	}
	if (blkdev_read_sect(gd, 0, rbuf) < 0) {
		printk(KERN_ERR "ide: smoke read %s LBA0 failed\n",
		       gd->disk_name);
		return;
	}
	printk(KERN_INFO "ide: smoke read %s LBA0 ok (via bio)\n",
	       gd->disk_name);
}

/* 把已探测磁盘挂入动态表（表本身随数量 kmalloc 增长） */
static int ide_register(struct ide_disk *d)
{
	struct ide_disk **ntab;
	int i;

	ntab = kmalloc((ide_ndisk + 1) * sizeof(*ntab));
	if (!ntab) {
		printk(KERN_ERR "ide: kmalloc disk table failed\n");
		return -1;
	}
	for (i = 0; i < ide_ndisk; i++) {
		ntab[i] = ide_tab[i];
	}
	ntab[ide_ndisk] = d;
	if (ide_tab) {
		kfree(ide_tab);
	}
	ide_tab = ntab;
	ide_ndisk++;
	return 0;
}

static void ide_probe_slot(const struct ide_ctl_desc *ctl, int chan, int unit)
{
	struct ide_disk probe;
	struct ide_disk *d;
	int st;

	probe.iobase = ctl->chan[chan].iobase;
	probe.altport = ctl->chan[chan].altport;
	probe.unit = unit;
	probe.sectors = 0;
	probe.model[0] = '\0';
	snprintf(probe.slot, sizeof(probe.slot), "%s.%d-%s",
		 ctl->name, chan, unit ? "slave" : "master");

	/*
	 * 选盘后读状态：0x00/0xFF 通常表示该通道无盘。
	 * QEMU：需 piix3-ide / isa-ide + ide-hd；VMware：若启动盘是 IDE 则会探测到它。
	 */
	outb(probe.iobase + IDE_DRIVE, IDE_SEL_LBA(unit));
	ide_delay(&probe);
	st = inb(probe.iobase + IDE_STATUS) & 0xff;
	if (st == 0xff || st == 0x00) {
		printk(KERN_WARNING
		       "ide: no disk on %s (status=0x%x)\n",
		       probe.slot, st);
		return;
	}

	if (ide_identify(&probe) < 0) {
		printk(KERN_WARNING
		       "ide: IDENTIFY failed on %s\n", probe.slot);
		return;
	}

	d = kmalloc(sizeof(*d));
	if (!d) {
		printk(KERN_ERR "ide: kmalloc disk for %s failed\n", probe.slot);
		return;
	}
	*d = probe;		/* 把 probe 的内容拷贝进这块堆内存 */
	d->gd = 0;
	d->drive = ide_ndisk;	/* 即将占用的下标 */

	if (ide_register(d) < 0) {
		kfree(d);
		return;
	}

	printk(KERN_INFO "ide: drive%d %s model (%s), %d sectors (%d KiB)\n",
	       d->drive, d->slot,
	       d->model[0] ? d->model : "(unknown)",
	       d->sectors,
	       d->sectors / 2);

	if (ide_add_gendisk(d) < 0) {
		printk(KERN_ERR "ide: add_disk failed for drive%d\n", d->drive);
		return;
	}

	ide_smoke_read(d->drive);
}

static void ide_probe_controller(const struct ide_ctl_desc *ctl)
{
	int chan, unit;

	printk(KERN_INFO "ide: scanning controller %s (%d channel(s))\n",
	       ctl->name, ctl->nchan);

	for (chan = 0; chan < ctl->nchan; chan++) {
		for (unit = 0; unit < IDE_NR_UNIT; unit++)
			ide_probe_slot(ctl, chan, unit);
	}
}

void ide_init(void)
{
	unsigned int i;

	initlock(&ide_lock, "ide");
	ide_tab = 0;
	ide_ndisk = 0;

	for (i = 0; i < sizeof(ide_ctls) / sizeof(ide_ctls[0]); i++)
		ide_probe_controller(&ide_ctls[i]);

	printk(KERN_INFO "ide: %d disk(s) registered\n", ide_ndisk);
}
