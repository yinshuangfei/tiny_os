#include "chrdev.h"
#include "../defs.h"
#include "../lock/spinlock.h"
#include "../printk.h"
#include "../major.h"

#define CHRDEV_MAJOR_MAX	256

static const struct file_operations *chrdevs[CHRDEV_MAJOR_MAX];
static const char *chrdev_names[CHRDEV_MAJOR_MAX];
static struct spinlock chrdev_lock;
static int chrdev_inited;

/*
 * 对齐 Linux fs/char_dev.c::chrdev_init：只初始化字符设备映射表。
 * 由 start_kernel/main 在驱动 register_chrdev 之前显式调用；
 * 不在此注册具体设备（console 等由各自驱动完成）。
 */
void chrdev_init(void)
{
	int i;

	if (chrdev_inited)
		return;
	initlock(&chrdev_lock, "chrdev");
	for (i = 0; i < CHRDEV_MAJOR_MAX; i++) {
		chrdevs[i] = 0;
		chrdev_names[i] = 0;
	}
	chrdev_inited = 1;
	printk(KERN_INFO "chrdev: subsystem ready\n");
}

int register_chrdev(unsigned int major, const char *name,
		    const struct file_operations *fops)
{
	int i;

	if (!chrdev_inited)
		panic("register_chrdev before chrdev_init");
	if (!name || !fops)
		return -1;

	acquire(&chrdev_lock);
	if (major == 0) {
		for (i = 1; i < CHRDEV_MAJOR_MAX; i++) {
			if (!chrdevs[i]) {
				major = (unsigned int)i;
				break;
			}
		}
		if (major == 0) {
			release(&chrdev_lock);
			return -1;
		}
	} else if (major >= CHRDEV_MAJOR_MAX) {
		release(&chrdev_lock);
		return -1;
	} else if (chrdevs[major]) {
		release(&chrdev_lock);
		printk(KERN_ERR "chrdev: major %u already registered (%s)\n",
		       major, chrdev_names[major] ? chrdev_names[major] : "?");
		return -1;
	}

	chrdevs[major] = fops;
	chrdev_names[major] = name;
	release(&chrdev_lock);

	printk(KERN_INFO "chrdev: registered major %u (%s)\n", major, name);
	return (int)major;
}

int unregister_chrdev(unsigned int major, const char *name)
{
	(void)name;
	if (!chrdev_inited || major == 0 || major >= CHRDEV_MAJOR_MAX)
		return -1;
	acquire(&chrdev_lock);
	chrdevs[major] = 0;
	chrdev_names[major] = 0;
	release(&chrdev_lock);
	return 0;
}

const struct file_operations *chrdev_get(unsigned int major)
{
	if (!chrdev_inited || major >= CHRDEV_MAJOR_MAX)
		return 0;
	return chrdevs[major];
}

const char *chrdev_name(unsigned int major)
{
	if (!chrdev_inited || major >= CHRDEV_MAJOR_MAX)
		return 0;
	return chrdev_names[major];
}

void chrdev_for_each(chrdev_iter_fn fn, void *arg)
{
	unsigned int i;

	if (!fn || !chrdev_inited)
		return;
	acquire(&chrdev_lock);
	for (i = 0; i < CHRDEV_MAJOR_MAX; i++) {
		if (chrdevs[i] && chrdev_names[i])
			fn(i, chrdev_names[i], arg);
	}
	release(&chrdev_lock);
}
