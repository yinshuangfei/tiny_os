/*
 * 内核字符串例程（对齐 Linux lib/string.c 教学子集）。
 */
#include "types.h"
#include "defs.h"

int strcmp(const char *a, const char *b)
{
	unsigned char ca, cb;

	while (*a && *a == *b) {
		a++;
		b++;
	}
	ca = (unsigned char)*a;
	cb = (unsigned char)*b;
	return (int)ca - (int)cb;
}

int strncmp(const char *a, const char *b, uint n)
{
	unsigned char ca, cb;

	if (n == 0)
		return 0;
	while (n > 0 && *a && *a == *b) {
		a++;
		b++;
		n--;
	}
	if (n == 0)
		return 0;
	ca = (unsigned char)*a;
	cb = (unsigned char)*b;
	return (int)ca - (int)cb;
}

char *strcpy(char *dst, const char *src)
{
	char *d = dst;

	while ((*d++ = *src++) != '\0')
		;
	return dst;
}

char *strncpy(char *dst, const char *src, uint n)
{
	char *d = dst;
	uint i;

	for (i = 0; i < n && src[i]; i++)
		d[i] = src[i];
	for (; i < n; i++)
		d[i] = '\0';
	return dst;
}

uint strlen(const char *s)
{
	uint n = 0;

	if (!s)
		return 0;
	while (s[n])
		n++;
	return n;
}
