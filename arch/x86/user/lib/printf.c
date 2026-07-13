/*
 * 用户态最小 printf：经 write(1, ...) 输出，无 libc 依赖。
 */
#include "user.h"
#include <stdarg.h>

static int putchar_fd(int fd, char c)
{
	return write(fd, &c, 1);
}

static int print_str(int fd, const char *s)
{
	int n = 0;

	if (!s)
		s = "(null)";
	while (*s) {
		if (putchar_fd(fd, *s++) < 0)
			return -1;
		n++;
	}
	return n;
}

static int print_uint(int fd, unsigned int val, unsigned int base, int upper)
{
	char buf[16];
	char *p = buf + sizeof(buf);
	int n = 0;
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

	if (base < 2 || base > 16)
		return -1;

	do {
		*--p = digits[val % base];
		val /= base;
	} while (val > 0);

	while (p < buf + sizeof(buf)) {
		if (putchar_fd(fd, *p++) < 0)
			return -1;
		n++;
	}
	return n;
}

static int print_int(int fd, int val)
{
	unsigned int u;
	int n = 0;

	if (val < 0) {
		if (putchar_fd(fd, '-') < 0)
			return -1;
		n++;
		u = (unsigned int)(-(val + 1)) + 1;
	} else {
		u = (unsigned int)val;
	}
	n += print_uint(fd, u, 10, 0);
	return n;
}

static int print_ptr(int fd, unsigned int val)
{
	int n = 0;

	if (putchar_fd(fd, '0') < 0)
		return -1;
	if (putchar_fd(fd, 'x') < 0)
		return -1;
	n = 2;
	n += print_uint(fd, val, 16, 0);
	return n;
}

static int vprintf_fd(int fd, const char *fmt, va_list ap)
{
	int n = 0;

	for (; *fmt; fmt++) {
		if (*fmt != '%') {
			if (putchar_fd(fd, *fmt) < 0)
				return -1;
			n++;
			continue;
		}

		fmt++;
		switch (*fmt) {
		case '%':
			if (putchar_fd(fd, '%') < 0)
				return -1;
			n++;
			break;
		case 'c': {
			int c = va_arg(ap, int);

			if (putchar_fd(fd, (char)c) < 0)
				return -1;
			n++;
			break;
		}
		case 's': {
			const char *s = va_arg(ap, const char *);
			int m = print_str(fd, s);

			if (m < 0)
				return -1;
			n += m;
			break;
		}
		case 'd':
		case 'i': {
			int m = print_int(fd, va_arg(ap, int));

			if (m < 0)
				return -1;
			n += m;
			break;
		}
		case 'u': {
			int m = print_uint(fd, va_arg(ap, unsigned int), 10, 0);

			if (m < 0)
				return -1;
			n += m;
			break;
		}
		case 'x':
		case 'X': {
			int m = print_uint(fd, va_arg(ap, unsigned int), 16,
					   *fmt == 'X');

			if (m < 0)
				return -1;
			n += m;
			break;
		}
		case 'p': {
			int m = print_ptr(fd, (unsigned int)va_arg(ap, void *));

			if (m < 0)
				return -1;
			n += m;
			break;
		}
		default:
			if (putchar_fd(fd, '%') < 0)
				return -1;
			if (putchar_fd(fd, *fmt) < 0)
				return -1;
			n += 2;
			break;
		}
	}
	return n;
}

int printf(const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vprintf_fd(1, fmt, ap);
	va_end(ap);
	return n;
}
