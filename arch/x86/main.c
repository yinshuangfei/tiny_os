#include "defs.h"

void main(void)
{
	serial_init();

	uart_puts("Hello Tiny-OS\n");
}
