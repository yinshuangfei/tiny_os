/*
 * PS/2 键盘驱动（扫描码集 1，对齐典型 PC/QEMU 默认）。
 * IRQ1 → 读 0x60 → 译码为 ASCII → console_intr() 投递到系统控制台输入缓冲。
 */
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "kbd.h"
#include "console.h"
#include "timer.h"

#define KBD_DATA	0x60	/* 键盘数据寄存器 */
#define KBD_STATUS	0x64	/* 键盘状态寄存器 */

#define KBD_STAT_OBF	0x01	/* Output Buffer Full: 输出缓冲有数据可读 */
#define KBD_STAT_IBF	0x02	/* Input Buffer Full: 输入缓冲忙，勿写命令 */

/* 修饰键（按下置位，释放清位） */
#define MOD_SHIFT	(1 << 0)
#define MOD_CTRL	(1 << 1)
#define MOD_ALT		(1 << 2)
#define MOD_CAPS	(1 << 3)

static int kbd_mod;		/* 修饰键状态 */
static int kbd_e0;		/* 收到 0xE0 前缀 */

/* 普通键：索引为 make code（释放码 = make|0x80） */
static const char kbd_map[128] = {
	[0x01] = 0x1b,	/* Esc */
	[0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
	[0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
	[0x0a] = '9', [0x0b] = '0', [0x0c] = '-', [0x0d] = '=',
	[0x0e] = '\b',	/* Backspace */
	[0x0f] = '\t',
	[0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
	[0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
	[0x18] = 'o', [0x19] = 'p', [0x1a] = '[', [0x1b] = ']',
	[0x1c] = '\n',	/* Enter */
	[0x1e] = 'a', [0x1f] = 's', [0x20] = 'd', [0x21] = 'f',
	[0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
	[0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
	[0x2b] = '\\',
	[0x2c] = 'z', [0x2d] = 'x', [0x2e] = 'c', [0x2f] = 'v',
	[0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
	[0x34] = '.', [0x35] = '/',
	[0x39] = ' ',
};

/* Shift 修饰键：索引为 make code（释放码 = make|0x80） */
static const char kbd_shift_map[128] = {
	[0x01] = 0x1b,
	[0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
	[0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
	[0x0a] = '(', [0x0b] = ')', [0x0c] = '_', [0x0d] = '+',
	[0x0e] = '\b',
	[0x0f] = '\t',
	[0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
	[0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
	[0x18] = 'O', [0x19] = 'P', [0x1a] = '{', [0x1b] = '}',
	[0x1c] = '\n',
	[0x1e] = 'A', [0x1f] = 'S', [0x20] = 'D', [0x21] = 'F',
	[0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
	[0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
	[0x2b] = '|',
	[0x2c] = 'Z', [0x2d] = 'X', [0x2e] = 'C', [0x2f] = 'V',
	[0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<',
	[0x34] = '>', [0x35] = '?',
	[0x39] = ' ',
};

/* 等待键盘状态寄存器可写 */
static void kbd_wait_write(void)
{
	int i;

	for (i = 0; i < 100000; i++) {
		if ((inb(KBD_STATUS) & KBD_STAT_IBF) == 0)
			return;
	}
}

/* 键盘扫描码译码 */
static int kbd_decode(int data)
{
	int key, release, shift;
	char c;

	/* 0xE0: 扩展键前缀 */
	if (data == 0xe0) {
		kbd_e0 = 1;
		return -1;
	}

	/* 释放键：高字节为 1 */
	release = data & 0x80;
	/* 键码：低字节为键码 */
	key = data & 0x7f;

	/* 左右 Shift / Ctrl / Alt（含 E0 扩展） */
	if (key == 0x2a || key == 0x36) {
		if (release)
			kbd_mod &= ~MOD_SHIFT;
		else
			kbd_mod |= MOD_SHIFT;
		kbd_e0 = 0;
		return -1;
	}
	/* Ctrl：仅 make 时置位 */
	if (key == 0x1d) {
		if (release)
			kbd_mod &= ~MOD_CTRL;
		else
			kbd_mod |= MOD_CTRL;
		kbd_e0 = 0;
		return -1;
	}
	/* Alt：仅 make 时置位 */
	if (key == 0x38) {
		if (release)
			kbd_mod &= ~MOD_ALT;
		else
			kbd_mod |= MOD_ALT;
		kbd_e0 = 0;
		return -1;
	}
	/* Caps Lock：仅 make 时翻转 */
	if (key == 0x3a) {
		if (!release)
			kbd_mod ^= MOD_CAPS;
		kbd_e0 = 0;
		return -1;
	}

	if (release || kbd_e0) {
		/* 扩展键（方向键等）暂忽略 */
		kbd_e0 = 0;
		return -1;
	}

	shift = (kbd_mod & MOD_SHIFT) != 0;
	if (shift)
		c = kbd_shift_map[key];
	else
		c = kbd_map[key];
	if (!c)
		return -1;

	/* Caps：对字母取反大小写 */
	if ((kbd_mod & MOD_CAPS) &&
	    ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
		if (c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 'A');
		else
			c = (char)(c - 'A' + 'a');
	}

	/* Ctrl+字母 → 控制字符 */
	if ((kbd_mod & MOD_CTRL) &&
	    ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
		if (c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 1);
		else
			c = (char)(c - 'A' + 1);
	}

	return (unsigned char)c;
}

/* 键盘中断处理 */
void kbd_intr(void)
{
	int data, c;

	pic_eoi(IRQ_1_KEYBOARD);

	while (inb(KBD_STATUS) & KBD_STAT_OBF) {
		data = inb(KBD_DATA) & 0xff;
		c = kbd_decode(data);
		if (c >= 0) {
			/* 主动触发系统控制台中断 */
			console_intr(c);
		}
	}
}

void kbd_init(void)
{
	kbd_mod = 0;
	kbd_e0 = 0;

	/* 排空 8042 残留 */
	while (inb(KBD_STATUS) & KBD_STAT_OBF)
		(void)inb(KBD_DATA);

	/* 写命令：使能键盘接口（无需等应答也能工作于 QEMU） */
	kbd_wait_write();
	/* 0xAE: Enable Keyboard: 使能键盘接口 */
	outb(KBD_STATUS, 0xae);

	printk(KERN_INFO "kbd: PS/2 keyboard ready (IRQ1)\n");
}
