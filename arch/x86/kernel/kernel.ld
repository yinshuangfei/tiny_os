/* QEMU -kernel 直接加载（K=1），内核链接并运行在 0x100000 */
LOAD_BASE = 0x100000;

SECTIONS
{
	. = LOAD_BASE;

	.text : {
		*(.text*)
	}

	.rodata : {
		*(.rodata*)
	}

	.data : {
		*(.data*)
		edata = .;
	}

	.bss : {
		*(.bss*)
		*(COMMON)
		end = .;
	}

	.note : ALIGN(4) {
		*(.note*)
	}

	. = ALIGN(4096);
}
