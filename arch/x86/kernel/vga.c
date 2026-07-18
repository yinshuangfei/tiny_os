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
#define VGA_CRT_INDEX	0x3d4
#define VGA_CRT_DATA	0x3d5

static volatile ushort *vga_fb = (volatile ushort *)VGA_BUF;
static int vga_pos;	/* 线性光标：row * COLS + col */

static void vga_set_cursor(int pos)
{
	outb(VGA_CRT_INDEX, 14);	/* cursor high */
	outb(VGA_CRT_DATA, (uchar)((pos >> 8) & 0xff));
	outb(VGA_CRT_INDEX, 15);	/* cursor low */
	outb(VGA_CRT_DATA, (uchar)(pos & 0xff));
}

/*
 * 滚动屏幕：将屏幕向上滚动一行。
 */
static void vga_scroll(void)
{
	int i;

	/* 上移一行 */
	for (i = 0; i < (VGA_ROWS - 1) * VGA_COLS; i++)
		vga_fb[i] = vga_fb[i + VGA_COLS];
	/* 清空最后一行 */
	for (i = (VGA_ROWS - 1) * VGA_COLS; i < VGA_ROWS * VGA_COLS; i++)
		vga_fb[i] = ((ushort)VGA_ATTR << 8) | ' ';
	vga_pos = (VGA_ROWS - 1) * VGA_COLS;
}

void vga_putc_at(int row, int col, char c)
{
	int pos;

	if (row < 0 || row >= VGA_ROWS || col < 0 || col >= VGA_COLS)
		return;
	pos = row * VGA_COLS + col;
	vga_fb[pos] = ((ushort)VGA_ATTR << 8) | (uchar)c;
}

/*
 * 控制台风格输出：处理 \\n / \\r / \\b / \\t，自动换行与滚动。
 */
void vga_putc(char c)
{
	if (c == '\n') {
		/* 换行：移动到下一行的开头 */
		vga_pos += VGA_COLS - (vga_pos % VGA_COLS);
	} else if (c == '\r') {
		/* 回车：移动到行首 */
		vga_pos -= vga_pos % VGA_COLS;
	} else if (c == '\b') {
		/* 退格：删除前一个字符 */
		if (vga_pos > 0) {
			vga_pos--;
			vga_fb[vga_pos] = ((ushort)VGA_ATTR << 8) | ' ';
		}
	} else if (c == '\t') {
		/* 制表符：跳过 8 个字符 */
		int n = 8 - (vga_pos % 8);

		while (n-- > 0)
			vga_putc(' ');
		return;
	} else {
		vga_fb[vga_pos] = ((ushort)VGA_ATTR << 8) | (uchar)c;
		vga_pos++;
	}

	if (vga_pos >= VGA_ROWS * VGA_COLS)
		vga_scroll();
	vga_set_cursor(vga_pos);
}

void vga_init(void)
{
	int i;

	for (i = 0; i < VGA_COLS * VGA_ROWS; i++)
		vga_fb[i] = ((ushort)VGA_ATTR << 8) | ' ';

	vga_pos = 0;
	vga_set_cursor(0);
}
