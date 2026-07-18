/*
 * 系统控制台：注册多个输出后端，内核日志与 /dev/console 共用。
 */
#include "types.h"
#include "defs.h"
#include "console.h"
#include "vga.h"

/* 全局控制台链表 */
static struct console *consoles;

/* ---------- ttyS0：串口 ---------- */
static void uart_console_write(struct console *con, const char *s,
			       unsigned int count)
{
	unsigned int i;

	(void)con;
	for (i = 0; i < count; i++)
		uart_putc(s[i]);
}

static struct console uart_console = {
	.name = "ttyS0",
	.write = uart_console_write,
};

/* ---------- tty0：VGA 文本 ---------- */
static void vga_console_write(struct console *con, const char *s,
			      unsigned int count)
{
	unsigned int i;

	(void)con;
	for (i = 0; i < count; i++)
		vga_putc(s[i]);
}

static struct console vga_console = {
	.name = "tty0",
	.write = vga_console_write,
};

void register_console(struct console *con)
{
	if (!con || !con->write)
		return;
	/* 将新控制台插入链表头部 */
	con->next = consoles;
	consoles = con;
}

void console_write(const char *s, unsigned int n)
{
	struct console *con;

	if (!s || n == 0)
		return;

	/* 遍历所有控制台，调用它们的 write 方法 */
	for (con = consoles; con; con = con->next)
		con->write(con, s, n);
}

void consputc(int c)
{
	char ch = (char)c;

	console_write(&ch, 1);
}

int console_getc(void)
{
	return uart_getc();
}

void console_init(void)
{
	/* 后注册的排在链表前；输出顺序：VGA 再串口（或反过来均可） */
	register_console(&uart_console);
	register_console(&vga_console);
}
