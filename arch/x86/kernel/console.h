#ifndef __CONSOLE_H__
#define __CONSOLE_H__

/*
 * 系统控制台（对齐 Linux printk console 思路的精简版）：
 *   - 多个 console 可注册；printk / printf / /dev/console 写同一路径
 *   - 当前后端：tty0（VGA 文本）+ ttyS0（COM1）
 *   - 输入：键盘与串口经 console_intr() 行规程后入缓冲，console_getc() 读取
 *   - 模式：默认 ICANON|ECHO|ISIG；ioctl(TCGETS/TCSETS) 可改
 *
 * 行规程标志与 user/include/termios.h 保持一致。
 */
#define CONS_ISIG	0x0001	/* 启用信号处理 */
#define CONS_ICANON	0x0002	/* 启用规范模式 */
#define CONS_ECHO	0x0008	/* 启用回显 */

#define CONS_TCGETS	1	/* 获取终端属性 */
#define CONS_TCSETS	2	/* 设置终端属性 */

struct console {
	char name[16];
	void (*write)(struct console *con, const char *s, unsigned int count);
	struct console *next;
};

void console_init(void);
/* 在 chrdev_init 之后调用：register_chrdev(/dev/console) */
void console_register_device(void);
void register_console(struct console *con);
void consputc(int c);
void console_write(const char *s, unsigned int n);
void console_intr(int c);
int console_getc(void);
int console_ioctl(unsigned int req, unsigned int uarg);
int console_is_canon(void);

/* 控制台前台进程（Ctrl+C → SIGINT 目标）；由 wait 维护，pid<=0 表示无 */
void console_set_fg(int pid);
int console_get_fg(void);

#endif
