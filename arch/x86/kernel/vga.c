/*
 * VGA 文本模式（CGA 兼容）：80 列 × 25 行，显存物理地址 0xb8000。
 * 每个字符单元 2 字节：低字节 ASCII，高字节属性（前景/背景色）。
 * 恒等映射下 VA == PA，分页开启后仍可直接写该地址。
 *
 * 识别 ANSI：清屏（Ctrl+L）与 SGR 颜色（shell C_RED 等）。
 */
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "vga.h"

#define VGA_BUF		0xb8000
#define VGA_COLS	80
#define VGA_ROWS	25

/*
 * 属性字节：
 *   bit0–2 前景 RGB（顺序为 B、G、R，与 ANSI 不同）
 *   bit3   前景高亮（对应 ANSI bold）
 *   bit4–6 背景 RGB
 *   bit7   闪烁 / 背景高亮（本驱动不用）
 *
 * - 0x07 ：浅灰字，黑底（最经典的 DOS/BIOS 默认配色）
 * - 0x02 ：绿灰字，黑底（黑客帝国配色）
 * - 0xf0 ：黑字，白底

 * 默认 0x07：浅灰字黑底（DOS 经典）
 */

 #define VGA_ATTR_THEME_DOS		0x07
 #define VGA_ATTR_THEME_HACKER		0x02
 #define VGA_ATTR_THEME_WHITE_BLACK	0xf0
#define VGA_ATTR_DEFAULT	VGA_ATTR_THEME_DOS

/* 6845 CRT 控制器（设置光标等） */
#define VGA_CRT_INDEX	0x3d4
#define VGA_CRT_DATA	0x3d5

/* 解析 shell 发出的 ANSI（如 \033[1;31m、\033[H\033[2J） */
enum {
	VT_NORM = 0,
	VT_ESC,		/* 已见 ESC */
	VT_CSI,		/* 已见 ESC [ */
};

#define CSI_NPARAM	8

/*
 * ANSI 颜色序号 0–7 → VGA 前景低 3 位。
 * ANSI：R=1 G=2 B=4；VGA：B=1 G=2 R=4。
 */
static const uchar ansi_to_vga[8] = {
	0,	/* black   */
	4,	/* red     */
	2,	/* green   */
	6,	/* yellow  */
	1,	/* blue    */
	5,	/* magenta */
	3,	/* cyan    */
	7,	/* white   */
};

static volatile ushort *vga_fb = (volatile ushort *)VGA_BUF;
static int vga_pos;

static uchar vga_fg;		/* ANSI 前景 0–7 */
static uchar vga_bg;		/* ANSI 背景 0–7 */
static int vga_bold;		/* SGR 1 */
static uchar vga_attr;		/* 当前写入用属性字节 */

static int vt_state;
static int csi_params[CSI_NPARAM];
static int csi_nparams;
static int csi_num;

static uchar vga_make_attr(void)
{
	uchar fg = ansi_to_vga[vga_fg & 7];
	uchar bg = ansi_to_vga[vga_bg & 7];

	if (vga_bold)
		fg |= 0x08;
	return (uchar)((bg << 4) | fg);
}

static void vga_attr_reset(void)
{
	vga_fg = 7;		/* 浅灰 / 白 */
	vga_bg = 0;
	vga_bold = 0;
	vga_attr = VGA_ATTR_DEFAULT;
}

static void vga_set_cursor(int pos)
{
	outb(VGA_CRT_INDEX, 14);	/* cursor high */
	outb(VGA_CRT_DATA, (uchar)((pos >> 8) & 0xff));
	outb(VGA_CRT_INDEX, 15);	/* cursor low */
	outb(VGA_CRT_DATA, (uchar)(pos & 0xff));
}

/* 清屏并把光标移到左上角（Ctrl+L / clear / \\f） */
static void vga_clear(void)
{
	int i;
	ushort blank = ((ushort)vga_attr << 8) | ' ';

	for (i = 0; i < VGA_COLS * VGA_ROWS; i++)
		vga_fb[i] = blank;
	vga_pos = 0;
	vga_set_cursor(0);
}

static void vga_scroll(void)
{
	int i;
	ushort blank = ((ushort)vga_attr << 8) | ' ';

	for (i = 0; i < (VGA_ROWS - 1) * VGA_COLS; i++)
		vga_fb[i] = vga_fb[i + VGA_COLS];
	for (i = (VGA_ROWS - 1) * VGA_COLS; i < VGA_ROWS * VGA_COLS; i++)
		vga_fb[i] = blank;
	vga_pos = (VGA_ROWS - 1) * VGA_COLS;
}

/* 移动光标到指定位置 */
static void vga_goto(int row, int col)
{
	if (row < 0)
		row = 0;
	if (col < 0)
		col = 0;
	if (row >= VGA_ROWS)
		row = VGA_ROWS - 1;
	if (col >= VGA_COLS)
		col = VGA_COLS - 1;
	vga_pos = row * VGA_COLS + col;
	vga_set_cursor(vga_pos);
}

/* 获取 CSI 参数 */
static int csi_param(int i, int def)
{
	if (i < csi_nparams && csi_params[i] > 0)
		return csi_params[i];
	return def;
}

/* SGR：ESC [ ... m —— 与 user/include/ansi.h 对齐 */
static void vga_sgr(void)
{
	int i, p, n;

	n = csi_nparams;
	if (n == 0) {
		vga_attr_reset();
		return;
	}

	for (i = 0; i < n; i++) {
		p = csi_params[i];
		if (p == 0) {
			vga_attr_reset();
		} else if (p == 1) {
			vga_bold = 1;
			vga_attr = vga_make_attr();
		} else if (p == 2 || p == 22) {
			vga_bold = 0;
			vga_attr = vga_make_attr();
		} else if (p >= 30 && p <= 37) {
			vga_fg = (uchar)(p - 30);
			vga_attr = vga_make_attr();
		} else if (p >= 40 && p <= 47) {
			vga_bg = (uchar)(p - 40);
			vga_attr = vga_make_attr();
		} else if (p == 39) {
			vga_fg = 7;
			vga_attr = vga_make_attr();
		} else if (p == 49) {
			vga_bg = 0;
			vga_attr = vga_make_attr();
		}
		/* 其余 SGR 忽略 */
	}
}

static void vga_csi_exec(char cmd)
{
	int row, col, mode;

	switch (cmd) {
	case 'H':
	case 'f':
		row = csi_param(0, 1) - 1;
		col = csi_param(1, 1) - 1;
		vga_goto(row, col);
		break;
	case 'J':
		mode = csi_param(0, 0);
		if (mode == 2 || mode == 3)
			vga_clear();
		break;
	case 'm':
		vga_sgr();
		break;
	default:
		break;
	}
}

static int vga_vt_feed(uchar c)
{
	if (vt_state == VT_NORM) {
		if (c == 0x1b) {
			vt_state = VT_ESC;
			return 1;
		}
		return 0;
	}

	if (vt_state == VT_ESC) {
		if (c == '[') {
			vt_state = VT_CSI;
			csi_nparams = 0;
			csi_num = -1;
			return 1;
		}
		vt_state = VT_NORM;
		return 1;
	}

	if (c >= '0' && c <= '9') {
		if (csi_num < 0)
			csi_num = 0;
		csi_num = csi_num * 10 + (c - '0');
		if (csi_num > 9999)
			csi_num = 9999;
		return 1;
	}
	if (c == ';') {
		if (csi_nparams < CSI_NPARAM)
			csi_params[csi_nparams++] = csi_num < 0 ? 0 : csi_num;
		csi_num = -1;
		return 1;
	}
	if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
		if (csi_nparams < CSI_NPARAM)
			csi_params[csi_nparams++] = csi_num < 0 ? 0 : csi_num;
		vga_csi_exec(c);
		vt_state = VT_NORM;
		return 1;
	}

	vt_state = VT_NORM;
	return 1;
}

void vga_putc_at(int row, int col, char c)
{
	int pos;

	if (row < 0 || row >= VGA_ROWS || col < 0 || col >= VGA_COLS)
		return;
	pos = row * VGA_COLS + col;
	vga_fb[pos] = ((ushort)vga_attr << 8) | (uchar)c;
}

void vga_putc(char c)
{
	uchar uc = (uchar)c;

	if (vga_vt_feed(uc))
		return;

	if (c == '\f') {
		vga_clear();
		return;
	}

	if (c == '\n') {
		vga_pos += VGA_COLS - (vga_pos % VGA_COLS);
	} else if (c == '\r') {
		vga_pos -= vga_pos % VGA_COLS;
	} else if (c == '\b') {
		if (vga_pos > 0) {
			vga_pos--;
			vga_fb[vga_pos] = ((ushort)vga_attr << 8) | ' ';
		}
	} else if (c == '\t') {
		int n = 8 - (vga_pos % 8);

		while (n-- > 0)
			vga_putc(' ');
		return;
	} else {
		vga_fb[vga_pos] = ((ushort)vga_attr << 8) | uc;
		vga_pos++;
	}

	if (vga_pos >= VGA_ROWS * VGA_COLS)
		vga_scroll();
	vga_set_cursor(vga_pos);
}

void vga_init(void)
{
	vt_state = VT_NORM;
	vga_attr_reset();
	vga_clear();
}
