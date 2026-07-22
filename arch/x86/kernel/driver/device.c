#include "device.h"
#include "../defs.h"
#include "../lock/spinlock.h"
#include "../printk.h"
#include "../mm/slab.h"

#define BLKDEV_MAJOR_MAX	256

static struct list_head buses;
static struct spinlock core_lock;
static int core_inited;

static const char *blkdev_names[BLKDEV_MAJOR_MAX];

struct bus_type platform_bus_type = {
	.name = "platform",
	.match = 0,
	.probe = 0,
};

static int streq(const char *a, const char *b)
{
	if (!a || !b)
		return 0;
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static int platform_match(struct device *dev, struct device_driver *drv)
{
	if (!dev || !drv || !dev->name || !drv->name)
		return 0;
	return streq(dev->name, drv->name);
}

static void bus_try_bind(struct bus_type *bus, struct device *dev)
{
	struct list_head *n;
	struct device_driver *drv;
	int ret;

	if (!bus || !dev || dev->driver)
		return;

	list_for_each(n, &bus->drivers) {
		drv = list_entry(n, struct device_driver, bus_list);
		if (bus->match) {
			if (!bus->match(dev, drv))
				continue;
		} else if (!platform_match(dev, drv)) {
			continue;
		}
		if (drv->probe) {
			ret = drv->probe(dev);
			if (ret < 0)
				continue;
		} else if (bus->probe) {
			ret = bus->probe(dev);
			if (ret < 0)
				continue;
		}
		dev->driver = drv;
		printk(KERN_INFO "driver: bound %s to %s on bus %s\n",
		       drv->name, dev->name, bus->name);
		return;
	}
}

void driver_core_init(void)
{
	if (core_inited)
		return;
	initlock(&core_lock, "driver");
	INIT_LIST_HEAD(&buses);
	INIT_LIST_HEAD(&platform_bus_type.devices);
	INIT_LIST_HEAD(&platform_bus_type.drivers);
	INIT_LIST_HEAD(&platform_bus_type.list);
	platform_bus_type.match = platform_match;
	core_inited = 1;
	bus_register(&platform_bus_type);
	printk(KERN_INFO "driver: core ready (bus/device/driver, chrdev)\n");
}

int bus_register(struct bus_type *bus)
{
	if (!core_inited)
		driver_core_init();
	if (!bus || !bus->name)
		return -1;
	acquire(&core_lock);
	if (list_empty(&bus->devices))
		INIT_LIST_HEAD(&bus->devices);
	if (list_empty(&bus->drivers))
		INIT_LIST_HEAD(&bus->drivers);
	list_add_tail(&bus->list, &buses);
	release(&core_lock);
	printk(KERN_INFO "driver: bus '%s' registered\n", bus->name);
	return 0;
}

struct bus_type *bus_find(const char *name)
{
	struct list_head *n;
	struct bus_type *b;

	if (!name || !core_inited)
		return 0;
	acquire(&core_lock);
	list_for_each(n, &buses) {
		b = list_entry(n, struct bus_type, list);
		if (streq(b->name, name)) {
			release(&core_lock);
			return b;
		}
	}
	release(&core_lock);
	return 0;
}

int driver_register(struct device_driver *drv)
{
	struct list_head *n;
	struct device *dev;

	if (!core_inited)
		driver_core_init();
	if (!drv || !drv->name || !drv->bus)
		return -1;

	acquire(&core_lock);
	/* 将驱动添加到总线驱动列表 */
	list_add_tail(&drv->bus_list, &drv->bus->drivers);
	/* 绑定已登记、尚未绑定的设备 */
	list_for_each(n, &drv->bus->devices) {
		dev = list_entry(n, struct device, bus_list);
		if (!dev->driver)
			bus_try_bind(drv->bus, dev);
	}
	release(&core_lock);

	printk(KERN_INFO "driver: '%s' on bus '%s'\n",
	       drv->name, drv->bus->name);
	return 0;
}

int device_register(struct device *dev)
{
	if (!core_inited)
		driver_core_init();
	if (!dev || !dev->name || !dev->bus)
		return -1;

	INIT_LIST_HEAD(&dev->bus_list);
	dev->driver = 0;

	acquire(&core_lock);
	/* 将设备添加到总线设备列表 */
	list_add_tail(&dev->bus_list, &dev->bus->devices);
	/* 尝试绑定设备和驱动 */
	bus_try_bind(dev->bus, dev);
	release(&core_lock);

	printk(KERN_INFO "driver: device '%s' on bus '%s'\n",
	       dev->name, dev->bus->name);
	if (dev->devt)
		printk(KERN_INFO "driver:   devt %u:%u\n",
		       MAJOR(dev->devt), MINOR(dev->devt));
	return 0;
}

void device_unregister(struct device *dev)
{
	if (!dev || !core_inited)
		return;
	acquire(&core_lock);
	if (dev->driver && dev->driver->remove)
		dev->driver->remove(dev);
	dev->driver = 0;
	list_del(&dev->bus_list);
	INIT_LIST_HEAD(&dev->bus_list);
	release(&core_lock);
}

void *dev_get_drvdata(const struct device *dev)
{
	return dev ? dev->driver_data : 0;
}

void dev_set_drvdata(struct device *dev, void *data)
{
	if (dev)
		dev->driver_data = data;
}

int register_blkdev(unsigned int major, const char *name)
{
	if (!name || major == 0 || major >= BLKDEV_MAJOR_MAX)
		return -1;
	if (!core_inited)
		driver_core_init();
	acquire(&core_lock);
	if (blkdev_names[major]) {
		release(&core_lock);
		return -1;
	}
	blkdev_names[major] = name;
	release(&core_lock);
	printk(KERN_INFO "blkdev: registered major %u (%s)\n", major, name);
	return 0;
}

const char *blkdev_name(unsigned int major)
{
	if (major >= BLKDEV_MAJOR_MAX)
		return 0;
	return blkdev_names[major];
}

void blkdev_for_each(blkdev_iter_fn fn, void *arg)
{
	unsigned int i;

	if (!fn)
		return;
	acquire(&core_lock);
	for (i = 0; i < BLKDEV_MAJOR_MAX; i++) {
		if (blkdev_names[i])
			fn(i, blkdev_names[i], arg);
	}
	release(&core_lock);
}
