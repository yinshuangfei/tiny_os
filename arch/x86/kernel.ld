ENTRY(_start)

SECTIONS
{
	. = 0x100000;

	.text : {
		*(.text*)
	}

	.rodata : {
		*(.rodata*)
	}

	.note : ALIGN(4) {
		*(.note*)
	}
}
