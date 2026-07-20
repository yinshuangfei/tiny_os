/*
 * 初级 IDE/ATA 硬盘驱动（PIO + IRQ 等待）。
 * 支持多 IDE 控制器（每控制器最多 2 通道 × master/slave）；LBA28。
 *
 * drive 为 ide_init 扫描后分配的逻辑盘号（0 .. ide_ndisks()-1）。
 * 探测成功后经 block 层注册 gendisk（hda, hdb, …），上层应走
 * submit_bio / blkdev_*；ide_read/write 仍为驱动底层接口。
 *
 * make_request 将 bio 入请求队列；每通道 runner 串行执行。
 * 运行期读写在 IF=1 时经 IRQ14/15（及 isa-ide 的 10/11）sleep 等待；
 * 探测阶段（sti 前）仍轮询 Status。
 */
#ifndef __IDE_H__
#define __IDE_H__

#include "types.h"

#define SECTSIZE	512

struct gendisk;

void ide_init(void);

/* trap.S 调用的 IRQ 入口 */
void ide_intr_irq10(void);
void ide_intr_irq11(void);
void ide_intr_irq14(void);
void ide_intr_irq15(void);

/* 已探测到的磁盘数量 */
int ide_ndisks(void);

/* 读写 1 个扇区；成功返回 0，失败返回 -1 */
int ide_read(int drive, uint lba, void *buf);
int ide_write(int drive, uint lba, const void *buf);

/* IDENTIFY 得到的总扇区数；无效 drive 时为 0 */
uint ide_nsectors(int drive);

/* 逻辑盘号对应的 gendisk；未注册则 NULL */
struct gendisk *ide_gendisk(int drive);

#endif
