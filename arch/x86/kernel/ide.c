/*
 * ATA PIO 驱动（教学用）：请求队列 + IRQ 等待。
 *
 * 每个 IDE 控制器最多 2 通道 × master/slave。
 * 通道端口由板级描述表给出；扫描到磁盘后再 kmalloc 结构体。
 * 命令：IDENTIFY(0xEC)、READ SECTORS(0x20)、WRITE SECTORS(0x30)。
 *
 * make_request 将 bio 封装为 request 入队；每通道一个 runner 串行执行。
 * 探测阶段（sti 前）仍轮询 Status；运行期 IF=1 时 ide_wait 经 IRQ sleep。
 */
#include "ide.h"
#include "defs.h"
#include "x86.h"
#include "lock/spinlock.h"
#include "lock/proc_lock.h"
#include "printk.h"
#include "block/blk.h"
#include "interrupt.h"
#include "proc.h"

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

/* Device Control（写 altport）：nIEN=0 允许 INTRQ；SRST=0 */
#define IDE_SEL_LBA(unit)	(0xe0 | (((unit) & 1) << 4))

#define IDE_NR_UNIT	2	/* 每通道 master/slave */
#define IDE_MAX_CHAN	8

/* 一个通道的命令口 / 备用状态口 / IRQ */
struct ide_chan_desc {
	ushort iobase;	/* 通道数据口基址 */
	ushort altport;	/* 备用状态口（Device Control） */
	int irq;	/* ISA IRQ 号 */
};

/*
 * 板级 IDE 控制器描述（端口固定，磁盘结构体动态分配）。
 *
 * ide0：PCI piix3-ide，兼容主/次通道 0x1F0 / 0x170 → IRQ14/15。
 * ide1：两路 isa-ide（Makefile irq=11 / irq=10）。
 */
struct ide_ctl_desc {
	const char *name;		/* 控制器名称 */
	struct ide_chan_desc chan[2];	/* 通道描述 */
	int nchan;			/* 通道数 */
};

/* 最多支持 8 块磁盘 */
static const struct ide_ctl_desc ide_ctls[] = {
	{
		"ide0",
		{
			{ 0x1f0, 0x3f6, IRQ_14_IDE0 },
			{ 0x170, 0x376, IRQ_15_IDE1 },
		},
		2,
	},
	{
		"ide1",
		{
			{ 0x1e8, 0x3ee, IRQ_11_IDE1A },
			{ 0x168, 0x36e, IRQ_10_IDE1B },
		},
		2,
	},
};

/* 运行时通道：IRQ 完成标志 + 请求队列 runner（每通道 outstanding=1，由 ide_lock 串行化） */
struct ide_chan {
	ushort iobase;		/* 通道数据口基址 */
	ushort altport;		/* 备用状态口 */
	int irq;		/* ISA IRQ 号 */
	volatile int done;	/* IRQ 置 1，等待方清 0，完成标志 */
	int busy;		/* 1：已有进程在 ide_run 本通道 */
	struct request *cur_rq;	/* 正在执行的 request（可 NULL） */
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
	struct ide_chan *chan;	/* 所属通道（IRQ 等待） */
};

static struct spinlock ide_lock;
static struct ide_disk **ide_tab;	/* 动态表：仅含已探测磁盘 */
static int ide_ndisk;			/* 已注册磁盘数 */
static struct ide_chan ide_chans[IDE_MAX_CHAN];
static int ide_nchan;			/* 已注册通道数 */
static int ide_irq_ready;		/* 已清 nIEN；真正 sleep 还须 IF=1 */

static struct ide_disk *ide_get(int drive)
{
	if (drive < 0 || drive >= ide_ndisk || !ide_tab)
		return 0;
	return ide_tab[drive];
}

static struct ide_chan *ide_chan_by_irq(int irq)
{
	int i;

	for (i = 0; i < ide_nchan; i++) {
		if (ide_chans[i].irq == irq)
			return &ide_chans[i];
	}
	return 0;
}

static struct ide_chan *ide_chan_get(ushort iobase, ushort altport, int irq)
{
	int i;
	struct ide_chan *c;

	for (i = 0; i < ide_nchan; i++) {
		if (ide_chans[i].iobase == iobase)
			return &ide_chans[i];
	}
	if (ide_nchan >= IDE_MAX_CHAN)
		return 0;
	c = &ide_chans[ide_nchan++];
	c->iobase = iobase;
	c->altport = altport;
	c->irq = irq;
	c->done = 0;
	c->busy = 0;
	c->cur_rq = 0;
	return c;
}

static void ide_delay(struct ide_disk *d)
{
	int i;

	/* 读状态口约 400ns，满足命令后最短等待 */
	for (i = 0; i < 4; i++) {
		(void)inb(d->altport);
	}
}

/*
 * 持有 ide_lock 进入；在 proc_lock 下释放后睡眠，唤醒后重新 acquire。
 * 与 uart/console 相同，避免丢失 wakeup（持锁期间 push_off 关中断）。
 */
static void sleep_chan(void *chan, struct spinlock *lk)
{
	struct proc *p = myproc();

	if (!holding(lk))
		panic("ide sleep_chan");
	if (!chan)
		panic("ide sleep_chan: null");
	if (!p)
		panic("ide sleep_chan: no proc");

	acquire(&proc_lock);
	release(lk);
	p->chan = chan;
	p->wakeup_tick = 0;
	p->state = SLEEPING;
	release(&proc_lock);
	sched();
	acquire(lk);
}

/* 轮询等待（探测 / IF=0 时使用） */
static int ide_wait_poll(struct ide_disk *d, int mask, int check_err)
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
 * 等待驱动就绪：IF 开启且通道已使能 IRQ 时 sleep；否则忙等。
 * 须持有 ide_lock。状态在循环内重读，IRQ 只负责 wakeup。
 */
static int ide_wait(struct ide_disk *d, int mask, int check_err)
{
	struct ide_chan *c;
	int r;
	int use_irq;

	use_irq = ide_irq_ready && intr_get() && d->chan && myproc();
	if (!use_irq)
		return ide_wait_poll(d, mask, check_err);

	c = d->chan;
	c->done = 0;
	for (;;) {
		r = inb(d->iobase + IDE_STATUS);
		if ((r & IDE_BSY) == 0 && (r & mask) == mask) {
			if (check_err && (r & (IDE_DF | IDE_ERR)) != 0)
				return -1;
			return r;
		}
		if (c->done) {
			c->done = 0;
			continue;
		}
		sleep_chan((void *)&c->done, &ide_lock);
		c->done = 0;
	}
}

/* IRQ 入口：读 STATUS 清锁存，唤醒等待者 */
static void ide_intr(int irq)
{
	struct ide_chan *c;

	pic_eoi(irq);
	c = ide_chan_by_irq(irq);
	if (!c)
		return;

	/* 读主 STATUS 确认并清除 pending INTRQ */
	(void)inb(c->iobase + IDE_STATUS);

	acquire(&ide_lock);
	c->done = 1;
	wakeup((void *)&c->done);
	release(&ide_lock);
}

void ide_intr_irq10(void)
{
	ide_intr(IRQ_10_IDE1B);
}

void ide_intr_irq11(void)
{
	ide_intr(IRQ_11_IDE1A);
}

void ide_intr_irq14(void)
{
	ide_intr(IRQ_14_IDE0);
}

void ide_intr_irq15(void)
{
	ide_intr(IRQ_15_IDE1);
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

static void ide_insw(struct ide_disk *d, void *buf, int words)
{
	ushort *p = buf;
	int i;

	for (i = 0; i < words; i++) {
		p[i] = inw(d->iobase + IDE_DATA);
	}
}

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

/* 从共享本通道的各盘队列中取队首 request（须持 ide_lock） */
static struct request *ide_chan_pick(struct ide_chan *c)
{
	int i;
	struct ide_disk *d;
	struct request *rq;

	for (i = 0; i < ide_ndisk; i++) {
		d = ide_tab[i];
		if (!d || d->chan != c || !d->gd || !d->gd->queue)
			continue;
		rq = blk_queue_pop(d->gd->queue);
		if (rq)
			return rq;
	}
	return 0;
}

static void ide_complete_rq(struct request *rq, blk_status_t st)
{
	struct bio *bio;

	if (!rq)
		return;
	bio = rq->bio;
	if (bio) {
		bio->bi_status = st;
		if (bio->bi_end_io)
			bio->bi_end_io(bio);
	}
	blk_free_request(rq);
}

/*
 * 执行单个 request（扇区循环走 ide_read/write，可 IRQ sleep）。
 * 不持 ide_lock；完成后由调用方继续 kick。
 */
static blk_status_t ide_execute_rq(struct request *rq)
{
	struct ide_disk *d;
	struct bio *bio;
	sector_t sec;
	unsigned int left;
	char *p;
	unsigned int op;
	int r;

	if (!rq || !rq->bio || !rq->q)
		return BLK_STS_IOERR;

	d = rq->q->queuedata;
	bio = rq->bio;
	if (!d)
		return BLK_STS_IOERR;

	sec = bio->bi_sector;
	left = bio->bi_size;
	p = bio->bi_data;
	op = bio->bi_opf & REQ_OP_MASK;

	while (left >= (unsigned int)SECTSIZE) {
		if (op == REQ_OP_WRITE)
			r = ide_write(d->drive, (uint)sec, p);
		else
			r = ide_read(d->drive, (uint)sec, p);
		if (r < 0)
			return BLK_STS_IOERR;
		sec++;
		p += SECTSIZE;
		left -= SECTSIZE;
	}
	if (left != 0)
		return BLK_STS_IOERR;
	return BLK_STS_OK;
}

/*
 * 通道队列 runner：串行取出 request 执行，直到本通道相关队列清空。
 * 调用前须已置 chan->busy=1；返回时清 busy。
 */
static void ide_run(struct ide_chan *c)
{
	struct request *rq;
	blk_status_t st;

	if (!c)
		return;

	for (;;) {
		acquire(&ide_lock);
		rq = ide_chan_pick(c);
		if (!rq) {
			c->busy = 0;
			c->cur_rq = 0;
			release(&ide_lock);
			return;
		}
		c->cur_rq = rq;
		release(&ide_lock);

		st = ide_execute_rq(rq);

		acquire(&ide_lock);
		c->cur_rq = 0;
		release(&ide_lock);

		/* end_io 可能唤醒 submit_bio_wait；勿持 ide_lock */
		ide_complete_rq(rq, st);
	}
}

/*
 * Linux 风格 make_request_fn：bio → request 入队；若通道空闲则 kick runner。
 * 由 submit_bio → d->gd->queue->make_request_fn 调用。
 * 立即返回（不等待 I/O）；调用方用 submit_bio_wait 睡眠等待。
 */
static void ide_submit_bio(struct request_queue *q, struct bio *bio)
{
	struct ide_disk *d;
	struct ide_chan *c;
	struct request *rq;
	int need_run;

	d = q ? q->queuedata : 0;
	if (!d || !bio || !d->chan) {
		if (bio) {
			bio->bi_status = BLK_STS_IOERR;
			if (bio->bi_end_io)
				bio->bi_end_io(bio);
		}
		return;
	}

	rq = blk_alloc_request(q);
	if (!rq) {
		bio->bi_status = BLK_STS_IOERR;
		if (bio->bi_end_io)
			bio->bi_end_io(bio);
		return;
	}
	rq->bio = bio;
	bio->bi_status = BLK_STS_OK;

	c = d->chan;
	acquire(&ide_lock);
	blk_queue_push(q, rq);
	need_run = !c->busy;
	if (need_run)
		c->busy = 1;
	release(&ide_lock);

	if (need_run)
		ide_run(c);
}

static int ide_add_gendisk(struct ide_disk *d)
{
	struct gendisk *gd;
	char name[8];

	gd = alloc_disk(1);
	if (!gd)
		return -1;

	gd->major = HD_MAJOR;
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

int ide_read(int drive, uint lba, void *buf)
{
	struct ide_disk *d;
	int r;

	d = ide_get(drive);
	if (!d || !buf)
		return -1;
	if (d->sectors && lba >= d->sectors)
		return -1;

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

int ide_write(int drive, uint lba, const void *buf)
{
	struct ide_disk *d;
	int r;

	d = ide_get(drive);
	if (!d || !buf)
		return -1;
	if (d->sectors && lba >= d->sectors)
		return -1;

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
	/* 写完等 BSY 清、设备就绪（完成 IRQ） */
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

	/* 探测在 sti 之前：强制轮询 */
	if (ide_wait_poll(d, IDE_DRQ, 1) < 0) {
		return -1;
	}
	ide_insw(d, id, 256);

	p = d->model;
	for (i = 0; i < 20; i++) {
		p[i * 2] = (char)(id[27 + i] >> 8);
		p[i * 2 + 1] = (char)(id[27 + i] & 0xff);
	}
	p[40] = '\0';
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
	struct ide_chan *c;
	int st;

	probe.iobase = ctl->chan[chan].iobase;
	probe.altport = ctl->chan[chan].altport;
	probe.unit = unit;
	probe.sectors = 0;
	probe.model[0] = '\0';
	probe.chan = 0;
	snprintf(probe.slot, sizeof(probe.slot), "%s.%d-%s",
		 ctl->name, chan, unit ? "slave" : "master");

	c = ide_chan_get(probe.iobase, probe.altport, ctl->chan[chan].irq);
	if (!c) {
		printk(KERN_ERR "ide: too many channels for %s\n", probe.slot);
		return;
	}
	probe.chan = c;

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
	d->chan = c;

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

/* 清 nIEN，允许通道产生 INTRQ */
static void ide_enable_channel_irq(struct ide_chan *c)
{
	/* bit1 nIEN=0；bit2 SRST=0 */
	outb(c->altport, 0x00);
	(void)inb(c->iobase + IDE_STATUS);	/* 清可能残留的 IRQ */
	c->done = 0;
}

void ide_init(void)
{
	unsigned int i;
	int c;

	initlock(&ide_lock, "ide");
	ide_tab = 0;
	ide_ndisk = 0;
	ide_nchan = 0;
	ide_irq_ready = 0;

	for (i = 0; i < sizeof(ide_ctls) / sizeof(ide_ctls[0]); i++)
		ide_probe_controller(&ide_ctls[i]);

	/* 探测完成后再开磁盘中断（IDENTIFY/smoke 在 sti 前，已走轮询） */
	for (c = 0; c < ide_nchan; c++)
		ide_enable_channel_irq(&ide_chans[c]);
	ide_irq_ready = 1;

	printk(KERN_INFO "ide: %d disk(s) registered, IRQ enabled\n", ide_ndisk);
}
