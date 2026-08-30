/* 高半区内核：低地址 bootstrap @ 1 MiB，主映像链接到 0xC0100000 */
LOAD_BASE = 0x00100000;
KERNEL_HIGH_LOAD_PA = 0x00102000;
KERNBASE = 0xC0000000;
KERNLINK = KERNBASE + KERNEL_HIGH_LOAD_PA;

SECTIONS
{
	. = LOAD_BASE;

	.text.boot : AT(LOAD_BASE) {
		*(.text.boot)
	}

	. = ALIGN(4096);

	.data.boot : {
		*(.data.boot)
	}

	. = KERNLINK;

	.text : AT(ADDR(.text) - KERNBASE) {
		*(.text*)
	}

	.rodata : AT(ADDR(.rodata) - KERNBASE) {
		*(.rodata*)
	}

	.data : AT(ADDR(.data) - KERNBASE) {
		*(.data*)
		edata = .;
	}

	.bss : AT(ADDR(.bss) - KERNBASE) {
		*(.bss*)
		*(COMMON)
		end = .;
	}

	. = ALIGN(4);

	.note : AT(ADDR(.note) - KERNBASE) {
		*(.note*)
	}

	. = ALIGN(4096);
}
