/* Unified linker script for K=0 (MBR @ 0x7e00) and K=1 (QEMU -kernel @ 0x100000).
 * Pass -defsym KERNEL=1 when linking the K=1 ELF; omit it for the MBR kernel.bin. */
LOAD_BASE = DEFINED(KERNEL) ? 0x100000 : 0x7e00;

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

	. = DEFINED(KERNEL) ? ALIGN(4096) : .;
}
