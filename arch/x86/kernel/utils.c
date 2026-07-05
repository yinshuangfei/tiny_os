/*
 * 字节 ↔ 人类可读大小（1024 进制，整数，K/M/G 后缀）
 */
#include "types.h"
#include "utils.h"

int bytes_to_human(uint64 bytes, char *buf, uint buflen)
{
	uint64 div;
	const char *unit;
	char tmp[16];
	uint n, len, i;

	if (!buf || buflen < 6)
		return -1;

	if (bytes >= (1UL << 30)) {
		div = 1UL << 30;
		unit = "GiB";
	} else if (bytes >= (1UL << 20)) {
		div = 1UL << 20;
		unit = "MiB";
	} else if (bytes >= (1UL << 10)) {
		div = 1UL << 10;
		unit = "KiB";
	} else {
		div = 1;
		unit = "B";
	}

	n = bytes / div;
	len = 0;
	do
		tmp[len++] = '0' + (n % 10);
	while ((n /= 10) > 0);

	i = 0;
	while (len > 0) {
		if (i + 1 >= buflen)
			return -1;
		buf[i++] = tmp[--len];
	}
	if (i + 1 >= buflen)
		return -1;
	buf[i++] = ' ';
	while (*unit) {
		if (i + 1 >= buflen)
			return -1;
		buf[i++] = *unit++;
	}
	buf[i] = '\0';
	return (int)i;
}

int human_to_bytes(const char *s, uint64 *bytes)
{
	uint64 n = 0;
	char c;

	if (!s || !bytes)
		return -1;

	while (*s >= '0' && *s <= '9')
		n = n * 10 + (*s++ - '0');

	if (*s == '\0') {
		*bytes = n;
		return 0;
	}

	c = *s++;
	if (c >= 'A' && c <= 'Z')
		c += 'a' - 'A';

	switch (c) {
	case 'g':
		n <<= 30;
		break;
	case 'm':
		n <<= 20;
		break;
	case 'k':
		n <<= 10;
		break;
	default:
		return -1;
	}

	if (*s == 'i' || *s == 'I')
		s++;
	if (*s == 'b' || *s == 'B')
		s++;
	if (*s != '\0')
		return -1;

	*bytes = n;
	return 0;
}
