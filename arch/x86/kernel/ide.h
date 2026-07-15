/*
 * 初级 IDE/ATA 硬盘驱动（PIO）。
 * 支持多 IDE 控制器（每控制器最多 2 通道 × master/slave）；LBA28。
 *
 * drive 为 ide_init 扫描后分配的逻辑盘号（0 .. ide_ndisks()-1）。
 */
#ifndef __IDE_H__
#define __IDE_H__

#include "types.h"

#define SECTSIZE	512

void ide_init(void);

/* 已探测到的磁盘数量 */
int ide_ndisks(void);

/* 读写 1 个扇区；成功返回 0，失败返回 -1 */
int ide_read(int drive, uint lba, void *buf);
int ide_write(int drive, uint lba, const void *buf);

/* IDENTIFY 得到的总扇区数；无效 drive 时为 0 */
uint ide_nsectors(int drive);

#endif
