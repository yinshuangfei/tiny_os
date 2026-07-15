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

static void print_str_to(printf_putc_t put, void *arg, const char *s)
{
	if (!s)
		s = "(null)";
	while (*s)
		put(*s++, arg);
}

static void print_uint_to(printf_putc_t put, void *arg, unsigned int val,
			  unsigned int base, int upper)
{
	char buf[16];
	char *p = buf + sizeof(buf);
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

	if (base < 2 || base > 16)
		return;

	do {
		*--p = digits[val % base];
		val /= base;
	} while (val > 0);

	while (p < buf + sizeof(buf))
		put(*p++, arg);
}

static void print_int_to(printf_putc_t put, void *arg, int val)
{
	unsigned int u;

	if (val < 0) {
		put('-', arg);
		u = (unsigned int)(-(val + 1)) + 1;
	} else {
		u = (unsigned int)val;
	}
	print_uint_to(put, arg, u, 10, 0);
}

static void print_ptr_to(printf_putc_t put, void *arg, unsigned int val)
{
	put('0', arg);
	put('x', arg);
	print_uint_to(put, arg, val, 16, 0);
}

static void vprintf_to(printf_putc_t put, void *arg, const char *fmt, va_list ap)
{
	for (; *fmt; fmt++) {
		if (*fmt != '%') {
			put(*fmt, arg);
			continue;
		}

		fmt++;
		switch (*fmt) {
		case '%':
			put('%', arg);
			break;
		case 'c':
			put((char)va_arg(ap, int), arg);
			break;
		case 's':
			print_str_to(put, arg, va_arg(ap, const char *));
			break;
		case 'd':
		case 'i':
			print_int_to(put, arg, va_arg(ap, int));
			break;
		case 'u':
			print_uint_to(put, arg, va_arg(ap, unsigned int), 10, 0);
			break;
		case 'x':
		case 'X':
			print_uint_to(put, arg, va_arg(ap, unsigned int), 16,
				      *fmt == 'X');
			break;
		case 'p':
			print_ptr_to(put, arg,
				     (unsigned int)va_arg(ap, void *));
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
