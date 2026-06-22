#include "defs.h"

#define BACKSPACE 0x100

void consputc(int c)
{
	if (c == BACKSPACE) {
		// if the user typed backspace, overwrite with a space.
		uartputc_sync('\b');
		uartputc_sync(' ');
		uartputc_sync('\b');
	} else {
		uartputc_sync(c);
	}
}