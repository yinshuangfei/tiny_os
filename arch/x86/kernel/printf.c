#include <stdarg.h>

#include "types.h"
#include "defs.h"
#include "printk.h"
#include "x86.h"
#include "spinlock.h"

volatile int panicked = 0;

/* 默认 INFO：打印 <= INFO 的消息，过滤 DEBUG */
int console_loglevel = LOGLEVEL_INFO;

static struct {
	struct spinlock lock;
	int locking;
} pr;

void consputc(int c)
{
	uart_putc(c);
}

static char digits[] = "0123456789abcdef";

static void printint(int xx, int base, int sign)
{
	char buf[16];
	int i;
	uint x;

	if(sign && (sign = xx < 0))
		x = -xx;
	else
		x = xx;

	i = 0;
	do {
		buf[i++] = digits[x % base];
	} while((x /= base) != 0);

	if(sign)
		buf[i++] = '-';

	while(--i >= 0)
		consputc(buf[i]);
}

static void printlongint(long int xx, int base, int sign)
{
	char buf[32];
	int i;
	uint64 x;

	if(sign && (sign = xx < 0))
		x = -xx;
	else
		x = xx;

	i = 0;
	do {
		buf[i++] = digits[x % base];
	} while((x /= base) != 0);

	if(sign)
		buf[i++] = '-';

	while(--i >= 0)
		consputc(buf[i]);
}

static void printptr(uint64 x)
{
	int i;
	consputc('0');
	consputc('x');

	for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
		consputc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

static void vprintf(const char *fmt, va_list ap)
{
	int i, c, c2;
	char *s;

	if (fmt == 0)
		panic("null fmt");

	for (i = 0; (c = fmt[i] & 0xff) != 0; i++) {
		if (c != '%') {
			consputc(c);
			continue;
		}

		c = fmt[++i] & 0xff;
		if (c == 0)
			break;

		switch (c) {
		case 'o':
			printint(va_arg(ap, int), 8, 1);
			break;
		case 'd':
			printint(va_arg(ap, int), 10, 1);
			break;
		case 'x':
			printint(va_arg(ap, int), 16, 1);
			break;
		case 'l':
			c2 = fmt[i+1] & 0xff;
			if (c2 == 'd') {
				printlongint(va_arg(ap, long int), 10, 1);
				i++;
			} else if (c2 == 'x') {
				printlongint(va_arg(ap, long int), 16, 1);
				i++;
			} else {
				consputc(c);
			}
			break;
		case 'p':
			printptr(va_arg(ap, uint64));
			break;
		case 's':
			if ((s = va_arg(ap, char*)) == 0)
				s = "(null)";
			for (; *s; s++)
				consputc(*s);
			break;
		case '%':
			consputc('%');
			break;
		default:
			consputc('%');
			consputc(c);
			break;
		}
	}
}

void printf(char *fmt, ...)
{
	va_list ap;
	int locking;

	locking = pr.locking;
	if (locking)
		acquire(&pr.lock);

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

	if (locking)
		release(&pr.lock);
}

/*
 * 解析 Linux 风格前缀 "<n>"，返回日志级别，并通过 *fmtp 跳过前缀。
 * 无前缀时默认 LOGLEVEL_INFO。
 */
static int printk_get_level(const char **fmtp)
{
	const char *fmt = *fmtp;

	if (fmt[0] == '<' && fmt[1] >= '0' && fmt[1] <= '7' && fmt[2] == '>') {
		*fmtp = fmt + 3;
		return fmt[1] - '0';
	}
	return LOGLEVEL_INFO;
}

int printk(const char *fmt, ...)
{
	va_list ap;
	int level, locking;

	if (!fmt)
		return 0;

	level = printk_get_level(&fmt);
	if (level > console_loglevel)
		return 0;

	locking = pr.locking;
	if (locking)
		acquire(&pr.lock);

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

	if (locking)
		release(&pr.lock);
	return 0;
}

void panic(char *s)
{
	pr.locking = 0;
	printf("panic: ");
	printf(s);
	printf("\n");
	panicked = 1;
	for (;;) halt();
}

void printfinit(void)
{
	initlock(&pr.lock, "pr");
	pr.locking = 1;
}
