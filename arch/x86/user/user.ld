/*
 * 用户程序链接脚本：映像从 USERBASE(0x400000) 起连续布局，
 * 含 .bss（填 0），供 objcopy -O binary / execve 整段加载。
 */
OUTPUT_FORMAT("elf32-i386")
ENTRY(_start)

SECTIONS
{
	. = 0x00400000;

	.text : {
		*(.text .text.*)
	}

	.rodata : {
		*(.rodata .rodata.*)
	}

	.data : {
		*(.data .data.*)
	}

	.bss : {
		*(.bss .bss.*)
		*(COMMON)
	}

	/* 供调试；flat binary 大小 = 此地址 - USERBASE */
	PROVIDE(user_end = .);
}
