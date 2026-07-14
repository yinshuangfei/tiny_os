/*
 * 初级 IDE/ATA 硬盘驱动（PIO，主总线 master）。
 * 端口：0x1F0–0x1F7 / 0x3F6；扇区 512 字节；LBA28。
 */
#ifndef __IDE_H__
#define __IDE_H__

#include "types.h"

#define SECTSIZE	512

void ide_init(void);

/* 读写 1 个扇区；成功返回 0，失败返回 -1 */
int ide_read(uint lba, void *buf);
int ide_write(uint lba, const void *buf);

/* IDENTIFY 得到的总扇区数；未初始化或失败时为 0 */
uint ide_nsectors(void);

#endif
