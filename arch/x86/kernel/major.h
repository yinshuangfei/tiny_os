/*
 * 主设备号与 dev_t 编解码（对齐 Linux include/uapi/linux/kdev_t.h + major.h）。
 *
 * 编码：12 bit major + 20 bit minor（共 32 bit），与现行 Linux 一致。
 *
 * 本内核已分配：
 *   major  名称             类型    节点
 *   ─────────────────────────────────────────
 *     0    UNNAMED_MAJOR    —       未命名 / 无效
 *     1    CONSOLE_MAJOR    字符    /dev/console
 *     3    HD_MAJOR         块      /dev/hda …
 *
 * 新增设备时在此表登记，禁止在驱动里散落魔法数字。
 */
#ifndef __MAJOR_H__
#define __MAJOR_H__

#include "types.h"

typedef uint32 dev_t;

/* Linux：MAJORBITS=12，MINORBITS=20 */
#define MAJORBITS		12
#define MINORBITS		20
#define MINORMASK		((1u << MINORBITS) - 1)

#define MAJOR(dev)		((unsigned int)((dev) >> MINORBITS))
#define MINOR(dev)		((unsigned int)((dev) & MINORMASK))
#define MKDEV(ma, mi)		(((dev_t)(ma) << MINORBITS) | ((mi) & MINORMASK))

/* ---------- 主设备号 ---------- */
#define UNNAMED_MAJOR		0
#define CONSOLE_MAJOR		1	/* 字符：控制台 */
#define HD_MAJOR		3	/* 块：IDE 整盘（Linux IDE0_MAJOR） */

/* 兼容旧名 */
#define IDE_MAJOR		HD_MAJOR
#define DEV_CONSOLE_MAJOR	CONSOLE_MAJOR
#define DEV_CONSOLE		CONSOLE_MAJOR

/* ---------- 次设备号（按设备约定） ---------- */
#define CONSOLE_MINOR		0
#define DEV_CONSOLE_MINOR	CONSOLE_MINOR

#endif
