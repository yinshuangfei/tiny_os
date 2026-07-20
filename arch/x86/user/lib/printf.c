/*
 * 用户态最小 printf / snprintf：经 write(1, ...) 或写缓冲，无 libc 依赖。
 */
#include "user.h"
#include <stdarg.h>

typedef void (*printf_putc_t)(int c, void *arg);

struct fd_ctx {
	int fd;		/* 文件描述符 */
	int n;		/* 已写入的字节数 */
	int err;	/* 错误标志 */
};

static void fd_putc(int c, void *arg)
{
	struct fd_ctx *ctx = arg;
	char ch = (char)c;

	if (ctx->err)
		return;
	if (write(ctx->fd, &ch, 1) < 0) {
		ctx->err = 1;
		return;
	}
	ctx->n++;
}

struct snbuf {
	char *buf;
	unsigned int size;
	unsigned int len;
};

static void sn_putc(int c, void *arg)
{
	struct snbuf *b = arg;

	if (b->buf && b->size > 0 && b->len + 1 < b->size)
		b->buf[b->len] = (char)c;
	b->len++;
}

static void pad_to(printf_putc_t put, void *arg, int width, int len, char pad)
{
	while (width > len) {
		put(pad, arg);
		width--;
	}
}

static void print_str_to(printf_putc_t put, void *arg, const char *s, int width)
{
	const char *p;
	int len;

	if (!s)
		s = "(null)";
	len = 0;
	for (p = s; *p; p++)
		len++;
	pad_to(put, arg, width, len, ' ');
	while (*s)
		put(*s++, arg);
}

static void print_uint_to(printf_putc_t put, void *arg, unsigned int val,
			  unsigned int base, int upper, int width, int zeropad)
{
	char buf[16];
	char *p = buf + sizeof(buf);
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	int len;

	if (base < 2 || base > 16)
		return;

	do {
		*--p = digits[val % base];
		val /= base;
	} while (val > 0);

	len = (int)((buf + sizeof(buf)) - p);
	pad_to(put, arg, width, len, zeropad ? '0' : ' ');
	while (p < buf + sizeof(buf))
		put(*p++, arg);
}

/*
 * 有符号十进制。宽度含符号：
 *   %5d  → 空格填充（"   -1"）
 *   %05d → 符号后零填充（"-0001"）
 */
static void print_int_to(printf_putc_t put, void *arg, int val, int width,
			 int zeropad)
{
	char buf[16];
	char *p = buf + sizeof(buf);
	unsigned int u;
	int neg, len;

	neg = val < 0;
	if (neg)
		u = (unsigned int)(-(val + 1)) + 1;
	else
		u = (unsigned int)val;

	do {
		*--p = (char)('0' + (u % 10));
		u /= 10;
	} while (u > 0);

	len = (int)((buf + sizeof(buf)) - p) + neg;
	if (zeropad) {
		if (neg)
			put('-', arg);
		pad_to(put, arg, width, len, '0');
	} else {
		pad_to(put, arg, width, len, ' ');
		if (neg)
			put('-', arg);
	}
	while (p < buf + sizeof(buf))
		put(*p++, arg);
}

static void print_ptr_to(printf_putc_t put, void *arg, unsigned int val)
{
	put('0', arg);
	put('x', arg);
	print_uint_to(put, arg, val, 16, 0, 0, 0);
}

/* 简易 %f：默认 4 位小数（无 libm） */
static void print_float_to(printf_putc_t put, void *arg, double val)
{
	int neg, ipart, i, digit;
	double frac;

	if (val != val) {
		print_str_to(put, arg, "nan", 0);
		return;
	}
	neg = 0;
	if (val < 0.0) {
		neg = 1;
		val = -val;
	}
	if (neg)
		put('-', arg);

	ipart = (int)val;
	print_int_to(put, arg, ipart, 0, 0);
	put('.', arg);

	frac = val - (double)ipart;
	for (i = 0; i < 4; i++) {
		frac *= 10.0;
		digit = (int)frac;
		if (digit < 0)
			digit = 0;
		if (digit > 9)
			digit = 9;
		put('0' + digit, arg);
		frac -= (double)digit;
	}
}

static void vprintf_to(printf_putc_t put, void *arg, const char *fmt, va_list ap)
{
	int width, zeropad;

	for (; *fmt; fmt++) {
		if (*fmt != '%') {
			put(*fmt, arg);
			continue;
		}

		fmt++;
		if (*fmt == 0)
			break;

		/* 标志 / 宽度：%5d、%05d */
		zeropad = 0;
		width = 0;
		if (*fmt == '0') {
			zeropad = 1;
			fmt++;
		}
		while (*fmt >= '0' && *fmt <= '9') {
			width = width * 10 + (*fmt - '0');
			fmt++;
		}
		if (*fmt == 0)
			break;

		switch (*fmt) {
		case '%':
			put('%', arg);
			break;
		case 'c':
			pad_to(put, arg, width, 1, ' ');
			put((char)va_arg(ap, int), arg);
			break;
		case 's':
			print_str_to(put, arg, va_arg(ap, const char *), width);
			break;
		case 'd':
		case 'i':
			print_int_to(put, arg, va_arg(ap, int), width, zeropad);
			break;
		case 'u':
			print_uint_to(put, arg, va_arg(ap, unsigned int), 10, 0,
				      width, zeropad);
			break;
		case 'x':
		case 'X':
			print_uint_to(put, arg, va_arg(ap, unsigned int), 16,
				      *fmt == 'X', width, zeropad);
			break;
		case 'p':
			print_ptr_to(put, arg,
				     (unsigned int)va_arg(ap, void *));
			break;
		case 'f':
			print_float_to(put, arg, va_arg(ap, double));
			break;
		default:
			put('%', arg);
			put(*fmt, arg);
			break;
		}
	}
}

int vsnprintf(char *buf, unsigned int size, const char *fmt, va_list ap)
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

int snprintf(char *buf, unsigned int size, const char *fmt, ...)
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
	return vsnprintf(buf, (unsigned int)-1, fmt, ap);
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

int printf(const char *fmt, ...)
{
	va_list ap;
	struct fd_ctx ctx;

	ctx.fd = 1;
	ctx.n = 0;
	ctx.err = 0;

	va_start(ap, fmt);
	vprintf_to(fd_putc, &ctx, fmt, ap);
	va_end(ap);

	return ctx.err ? -1 : ctx.n;
}
