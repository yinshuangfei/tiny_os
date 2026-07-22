/*
 * 设备模型核心（对齐 Linux drivers/base 教学子集）：
 *   bus_type → device ↔ device_driver（match/probe）
 * 不实现 sysfs/devtmpfs；供 IDE 等驱动登记与 /proc/devices 枚举。
 */
#ifndef __DRIVER_DEVICE_H__
#define __DRIVER_DEVICE_H__

#include "../types.h"
#include "../list.h"
#include "../major.h"

struct device;
struct device_driver;
struct bus_type;

struct bus_type {
	const char *name;			/* 总线名称 */
	struct list_head devices;		/* struct device::bus_list */
	struct list_head drivers;		/* struct device_driver::bus_list */
	struct list_head list;			/* 全局 buses */
	int (*match)(struct device *dev, struct device_driver *drv);	/* 匹配设备和驱动 */
	int (*probe)(struct device *dev);	/* 探测设备 */
};

struct device_driver {
	const char *name;			/* 驱动名称 */
	struct bus_type *bus;			/* 所属总线 */
	int (*probe)(struct device *dev);	/* 探测设备 */
	int (*remove)(struct device *dev);	/* 移除设备 */
	struct list_head bus_list;		/* 总线列表 */
};

struct device {
	const char *name;			/* 设备名称 */
	struct bus_type *bus;			/* 所属总线 */
	struct device_driver *driver;		/* 所属驱动 */
	void *driver_data;			/* 驱动数据 */
	dev_t devt;			/* 可选：关联 MKDEV(major,minor) */
	struct list_head bus_list;		/* 总线列表 */
};

void driver_core_init(void);

int bus_register(struct bus_type *bus);
struct bus_type *bus_find(const char *name);

int driver_register(struct device_driver *drv);
int device_register(struct device *dev);
void device_unregister(struct device *dev);

void *dev_get_drvdata(const struct device *dev);
void dev_set_drvdata(struct device *dev, void *data);

/* 内置 platform 总线（无硬件枚举，仅作设备挂靠） */
extern struct bus_type platform_bus_type;

/* 块主设备号命名（对齐 register_blkdev 教学子集） */
int register_blkdev(unsigned int major, const char *name);
const char *blkdev_name(unsigned int major);
typedef void (*blkdev_iter_fn)(unsigned int major, const char *name, void *arg);
void blkdev_for_each(blkdev_iter_fn fn, void *arg);

#endif
