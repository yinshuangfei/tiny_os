/*
 * 字符设备注册表（对齐 Linux fs/char_dev.c 教学子集）。
 * open(/dev/xxx) → MAJOR(rdev) 查表 → file_operations。
 */
#ifndef __DRIVER_CHRDEV_H__
#define __DRIVER_CHRDEV_H__

struct file;

/*
 * 字符设备操作（对齐 Linux file_operations 子集）。
 * 未实现的回调可为 NULL（返回 -1）。
 */
struct file_operations {
	int (*read)(struct file *f, char *dst, int n);
	int (*write)(struct file *f, char *src, int n);
	int (*ioctl)(struct file *f, unsigned int req, unsigned int arg);
	int (*open)(struct file *f);
	int (*release)(struct file *f);
};

void chrdev_init(void);

/*
 * 注册主设备号 major 的字符驱动（须在 chrdev_init 之后）。
 * major==0 时自动分配（返回分配到的 major，失败 <0）。
 */
int register_chrdev(unsigned int major, const char *name,
		    const struct file_operations *fops);
int unregister_chrdev(unsigned int major, const char *name);

const struct file_operations *chrdev_get(unsigned int major);
const char *chrdev_name(unsigned int major);

/* /proc/devices 用：遍历已注册字符主设备号 */
typedef void (*chrdev_iter_fn)(unsigned int major, const char *name, void *arg);
void chrdev_for_each(chrdev_iter_fn fn, void *arg);

#endif
