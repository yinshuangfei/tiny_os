/*
 * termios 包装：tcgetattr / tcsetattr → ioctl(TCGETS/TCSETS)。
 */
#include "user.h"
#include "termios.h"

int tcgetattr(int fd, struct termios *tio)
{
	if (!tio)
		return -1;
	return ioctl(fd, TCGETS, tio);
}

int tcsetattr(int fd, int optional_actions, const struct termios *tio)
{
	(void)optional_actions;
	if (!tio)
		return -1;
	return ioctl(fd, TCSETS, (void *)tio);
}
