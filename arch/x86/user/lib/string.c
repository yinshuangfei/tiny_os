/*
 * 用户态最小字符串函数。
 */
#include "user.h"

unsigned int strlen(const char *s)
{
	unsigned int n = 0;

	if (!s)
		return 0;
	while (s[n])
		n++;
	return n;
}

int strcmp(const char *a, const char *b)
{
	unsigned char ca, cb;

	if (!a)
		a = "";
	if (!b)
		b = "";
	while (*a && *a == *b) {
		a++;
		b++;
	}
	ca = (unsigned char)*a;
	cb = (unsigned char)*b;
	return (int)ca - (int)cb;
}

int strncmp(const char *a, const char *b, unsigned int n)
{
	unsigned char ca, cb;

	if (n == 0)
		return 0;
	if (!a)
		a = "";
	if (!b)
		b = "";
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

	if (!dst)
		return dst;
	if (!src)
		src = "";
	while ((*d++ = *src++) != '\0')
		;
	return dst;
}

char *strncpy(char *dst, const char *src, unsigned int n)
{
	char *d = dst;
	unsigned int i;

	if (!dst || n == 0)
		return dst;
	if (!src)
		src = "";
	for (i = 0; i < n && src[i]; i++)
		d[i] = src[i];
	for (; i < n; i++)
		d[i] = '\0';
	return dst;
}
