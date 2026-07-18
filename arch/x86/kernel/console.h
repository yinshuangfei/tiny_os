#ifndef __CONSOLE_H__
#define __CONSOLE_H__

/*
 * 系统控制台（对齐 Linux printk console 思路的精简版）：
 *   - 多个 console 可注册；printk / printf / /dev/console 写同一路径
 *   - 当前后端：tty0（VGA 文本）+ ttyS0（COM1）
 *   - 输入：键盘与串口经 console_intr() 汇入同一缓冲，console_getc() 读取
 */
struct console {
	char name[16];
	void (*write)(struct console *con, const char *s, unsigned int count);
	struct console *next;
};

void console_init(void);
void register_console(struct console *con);
void consputc(int c);
void console_write(const char *s, unsigned int n);
void console_intr(int c);
int console_getc(void);

#endif
