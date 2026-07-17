/*
 * VGA 文本模式（CGA 兼容）：80 列 × 25 行，显存物理地址 0xb8000。
 * 每个字符单元 2 字节：低字节 ASCII，高字节属性（前景/背景色）。
 * 恒等映射下 VA == PA，分页开启后仍可直接写该地址。
 */
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "vga.h"

#define VGA_BUF		0xb8000
#define VGA_COLS	80
#define VGA_ROWS	25
#define VGA_ATTR	0x0f	/* 白字黑底 */

/* 6845 CRT 控制器（设置光标等） */
#define VGA_CRT_INDEX	0x3d4		/* INDEX 端口地址 */
#define VGA_CRT_DATA	0x3d5		/* DATA 端口地址 */

static volatile ushort *vga_fb = (volatile ushort *)VGA_BUF;

static void vga_set_cursor(int pos)
{
	outb(VGA_CRT_INDEX, 14);	/* cursor high */
	outb(VGA_CRT_DATA, (uchar)((pos >> 8) & 0xff));
	outb(VGA_CRT_INDEX, 15);	/* cursor low */
	outb(VGA_CRT_DATA, (uchar)(pos & 0xff));
}

void vga_putc_at(int row, int col, char c)
{
	int pos;

	if (row < 0 || row >= VGA_ROWS || col < 0 || col >= VGA_COLS)
		return;
	pos = row * VGA_COLS + col;
	vga_fb[pos] = ((ushort)VGA_ATTR << 8) | (uchar)c;
}

void vga_init(void)
{
	int i;

	/* 清屏 */
	for (i = 0; i < VGA_COLS * VGA_ROWS; i++)
		vga_fb[i] = ((ushort)VGA_ATTR << 8) | ' ';

	vga_set_cursor(0);

	/* 在画面左上角输出一个字符，便于 VNC/VGA 确认显示路径 */
	vga_putc_at(0, 0, 'A');
}
