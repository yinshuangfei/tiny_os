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

/* 输出回调：控制台或 snprintf 缓冲 */
typedef void (*printf_putc_t)(int c, void *arg);

static void printint_to(printf_putc_t put, void *arg, int xx, int base, int sign)
{
	char buf[16];
	int i;
	uint x;

	if (sign && (sign = xx < 0))
		x = -xx;
	else
		x = xx;

	i = 0;
	do {
		buf[i++] = digits[x % base];
	} while ((x /= base) != 0);

	if (sign)
		buf[i++] = '-';

	while (--i >= 0)
		put(buf[i], arg);
}

static void printlongint_to(printf_putc_t put, void *arg, long int xx, int base,
			    int sign)
{
	char buf[32];
	int i;
	uint64 x;

	if (sign && (sign = xx < 0))
		x = -xx;
	else
		x = xx;

	i = 0;
	do {
		buf[i++] = digits[x % base];
	} while ((x /= base) != 0);

	if (sign)
		buf[i++] = '-';

	while (--i >= 0)
		put(buf[i], arg);
}

static void printptr_to(printf_putc_t put, void *arg, uint64 x)
{
	int i;

	put('0', arg);
	put('x', arg);
	for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
		put(digits[x >> (sizeof(uint64) * 8 - 4)], arg);
}

static void vprintf_to(printf_putc_t put, void *arg, const char *fmt, va_list ap)
{
	int i, c, c2;
	char *s;

	if (fmt == 0)
		panic("null fmt");

	for (i = 0; (c = fmt[i] & 0xff) != 0; i++) {
		if (c != '%') {
			put(c, arg);
			continue;
		}

		c = fmt[++i] & 0xff;
		if (c == 0)
			break;

		switch (c) {
		case 'o':
			printint_to(put, arg, va_arg(ap, int), 8, 1);
			break;
		case 'd':
			printint_to(put, arg, va_arg(ap, int), 10, 1);
			break;
		case 'x':
			printint_to(put, arg, va_arg(ap, int), 16, 1);
			break;
		case 'l':
			c2 = fmt[i + 1] & 0xff;
			if (c2 == 'd') {
				printlongint_to(put, arg, va_arg(ap, long int),
						10, 1);
				i++;
			} else if (c2 == 'x') {
				printlongint_to(put, arg, va_arg(ap, long int),
						16, 1);
				i++;
			} else {
				put(c, arg);
			}
			break;
		case 'p':
			printptr_to(put, arg, va_arg(ap, uint64));
			break;
		case 's':
			if ((s = va_arg(ap, char *)) == 0)
				s = "(null)";
			for (; *s; s++)
				put(*s, arg);
			break;
		case '%':
			put('%', arg);
			break;
		default:
			put('%', arg);
			put(c, arg);
			break;
		}
	}
}

static void cons_putc_arg(int c, void *arg)
{
	(void)arg;
	consputc(c);
}

/* 用于 snprintf 的缓冲 */
struct snbuf {
	char *buf;
	uint size;	/* 含结尾 '\\0' 的容量 */
	uint len;	/* 已写入/本应写入的字符数（不含 '\\0'） */
};

/* 用于 snprintf 的输出回调 */
static void sn_putc(int c, void *arg)
{
	struct snbuf *b = arg;

	if (b->buf && b->size > 0 && b->len + 1 < b->size)
		b->buf[b->len] = (char)c;
	b->len++;
}

/*
 * C99：返回若不受限本应写入的字符数（不含 '\\0'）。
 * size>0 时保证 buf 以 '\\0' 结尾（即便截断）。
 */
int vsnprintf(char *buf, uint size, const char *fmt, va_list ap)
{
	struct snbuf b;

	b.buf = buf;
	b.size = size;
	b.len = 0;
	vprintf_to(sn_putc, &b, fmt, ap);
	if (buf && size > 0) {
		if (b.len < size)
			buf[b.len] = '\0';
		else
			buf[size - 1] = '\0';
	}
	return (int)b.len;
}

int snprintf(char *buf, uint size, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return n;
}

/* 无边界检查；调用方须保证 buf 足够大。优先用 snprintf。 */
int vsprintf(char *buf, const char *fmt, va_list ap)
{
	return vsnprintf(buf, (uint)-1, fmt, ap);
}

int sprintf(char *buf, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsprintf(buf, fmt, ap);
	va_end(ap);
	return n;
}

static void vprintf(const char *fmt, va_list ap)
{
	vprintf_to(cons_putc_arg, 0, fmt, ap);
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
	for (;;)
		halt();
}

void printfinit(void)
{
	initlock(&pr.lock, "pr");
	pr.locking = 1;
}
