/*
 * VGA 文本模式（CGA 兼容）：80 列 × 25 行，显存物理地址 0xb8000。
 * 每个字符单元 2 字节：低字节 ASCII，高字节属性（前景/背景色）。
 * 恒等映射下 VA == PA，分页开启后仍可直接写该地址。
 *
 * 另识别少量控制/ANSI，使 shell Ctrl+L（ANSI_CLEAR）在 VGA 上也能清屏。
 */
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "vga.h"

#define VGA_BUF		0xb8000
#define VGA_COLS	80
#define VGA_ROWS	25

/*
 * 低 4 位 (Bit 0 - 3)：前景色（Foreground Color）
 * 高 4 位 (Bit 4 - 7)：背景色（Background Color）
 *
 * - 0x07 ：浅灰字，黑底（最经典的 DOS/BIOS 默认配色）
 * - 0x02 ：绿灰字，黑底（黑客帝国配色）
 * - 0xf0 ：黑字，白底
 */
#define VGA_ATTR_THEME_DOS		0x07
#define VGA_ATTR_THEME_HACKER		0x02
#define VGA_ATTR_THEME_WHITE_BLACK	0xf0

#define VGA_ATTR	VGA_ATTR_THEME_DOS

/* 6845 CRT 控制器（设置光标等） */
#define VGA_CRT_INDEX	0x3d4
#define VGA_CRT_DATA	0x3d5

/* 解析 shell 发出的 ANSI（如 \033[H\033[2J） */
enum {
	VT_NORM = 0,
	VT_ESC,		/* 已见 ESC */
	VT_CSI,		/* 已见 ESC [ */
};

#define CSI_NPARAM	4

static volatile ushort *vga_fb = (volatile ushort *)VGA_BUF;
static int vga_pos;	/* 线性光标：row * COLS + col */

static int vt_state;
static int csi_params[CSI_NPARAM];
static int csi_nparams;
static int csi_num;		/* 当前正在读的十进制参数，-1=尚未开始 */

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

	for (i = 0; i < VGA_COLS * VGA_ROWS; i++)
		vga_fb[i] = ((ushort)VGA_ATTR << 8) | ' ';
	vga_pos = 0;
	vga_set_cursor(0);
}

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

/* 解析 CSI 参数 */
static int csi_param(int i, int def)
{
	if (i < csi_nparams && csi_params[i] > 0)
		return csi_params[i];
	return def;
}

/* 结束一条 CSI，执行命令字母 cmd */
static void vga_csi_exec(char cmd)
{
	int row, col, mode;

	switch (cmd) {
	case 'H':	/* CUP：光标定位；无参 = 回家 */
	case 'f':
		row = csi_param(0, 1) - 1;
		col = csi_param(1, 1) - 1;
		vga_goto(row, col);
		break;
	case 'J':	/* ED：擦除显示 */
		mode = csi_param(0, 0);
		if (mode == 2 || mode == 3)
			vga_clear();
		break;
	default:
		/* 未实现的 CSI（颜色等）静默忽略，避免刷屏乱码 */
		break;
	}
}

/*
 * 吃掉 ANSI 转义字节；返回 1 表示已消费，勿当普通字符显示。
 * 支持 shell ANSI_CLEAR：ESC [ H  +  ESC [ 2 J
 */
static int vga_vt_feed(uchar c)
{
	/* 正常状态：等待 ESC */
	if (vt_state == VT_NORM) {
		/* ESC：开始转义序列 */
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
		return 1;	/* 孤立 ESC，丢弃 */
	}

	/* VT_CSI */
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
	/* 命令字母结束序列 */
	if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
		if (csi_nparams < CSI_NPARAM)
			csi_params[csi_nparams++] = csi_num < 0 ? 0 : csi_num;
		vga_csi_exec(c);
		vt_state = VT_NORM;
		return 1;
	}

	/* 非法字节，退出 CSI */
	vt_state = VT_NORM;
	return 1;
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
 * 控制台风格输出：
 *   \\n / \\r / \\b / \\t、换行滚动；
 *   \\f（Ctrl+L）与 ANSI 清屏序列清屏归位。
 */
void vga_putc(char c)
{
	uchar uc = (uchar)c;

	/* 解析 ANSI 转义字符 */
	if (vga_vt_feed(uc))
		return;

	/* 处理特殊字符 */
	if (c == '\f') {
		/* Form Feed / Ctrl+L：清屏并回家 */
		vga_clear();
		return;
	}

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
		vga_fb[vga_pos] = ((ushort)VGA_ATTR << 8) | uc;
		vga_pos++;
	}

	if (vga_pos >= VGA_ROWS * VGA_COLS)
		vga_scroll();
	vga_set_cursor(vga_pos);
}

void vga_init(void)
{
	vt_state = VT_NORM;
	vga_clear();
}
