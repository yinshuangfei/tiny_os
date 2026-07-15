/*
 * 简易用户态 shell：从 stdin（串口）读命令，内置 help/echo/pid/cat/ls/exit/color/clear，
 * 方向键：上下历史，左右移动光标（可在光标处插入）；
 * Ctrl+←/→ 按单词移动；Ctrl+A/E 行首行尾，Ctrl+L 清屏，Ctrl+W 删词。
 * 其它名称经 fork + execve + waitpid 执行。
 * 提示符 / 帮助 / 错误使用 ANSI 颜色（对端终端解释，同 Linux）；
 * 可用内置命令 color on|off 开关。
 *
 * 结构贴近 bash 教学版：PS1 提示符、行内分词为 argv、内置命令表分发。
 * bash 本体：十几万行代码级。
 */
#include "user.h"
#include "ansi.h"

#define BUFSZ		128
#define HISTMAX		32
#define MAXARGV		32
#define NELEM(a)	(sizeof(a) / sizeof((a)[0]))

/* readline 控制字符（同 ASCII / bash readline 绑定） */
#define CTRL_A		0x01	/* beginning-of-line */
#define CTRL_E		0x05	/* end-of-line */
#define CTRL_L		0x0c	/* clear-screen */
#define CTRL_W		0x17	/* unix-word-rubout */
#define KEY_ESC		0x1b
#define KEY_BACKSPACE	0x08
#define KEY_DELETE	0x7f

static char history[HISTMAX][BUFSZ];
static int hist_n;		/* 已保存条数 */

static int is_space(char c)
{
	return c == ' ' || c == '\t';
}

/* 跳过空白字符 */
static char *skipsp(char *s)
{
	while (*s && is_space(*s)) {
		s++;
	}
	return s;
}

/* 有界拷贝并保证 '\0' 结尾 */
static void strlcpy_local(char *dst, const char *src, int max)
{
	if (max <= 0) {
		return;
	}
	strncpy(dst, src, (unsigned int)(max - 1));
	dst[max - 1] = '\0';
}

/*
 * 原地分词：把 line 拆成 argv，空白处改写成 '\0'。
 * 返回 argc，且 argv[argc] = NULL。
 */
static int split_argv(char *line, char **argv, int max)
{
	int argc = 0;
	char *p = skipsp(line);

	while (*p && argc + 1 < max) {
		argv[argc++] = p;
		while (*p && !is_space(*p)) {
			p++;
		}
		if (!*p) {
			break;
		}
		*p++ = '\0';
		p = skipsp(p);
	}
	argv[argc] = 0;
	return argc;
}

/* ---------- UTF-8 / 显示宽度（终端列） ---------- */

/* 首字节对应的 UTF-8 期望长度；非法首字节按 1 */
static int utf8_expected_len(unsigned char c)
{
	if (c < 0x80)
		return 1;
	if ((c & 0xe0) == 0xc0)
		return 2;
	if ((c & 0xf0) == 0xe0)
		return 3;
	if ((c & 0xf8) == 0xf0)
		return 4;
	return 1;
}

/* 从 s 起一个 UTF-8 字符的字节数；非法序列按 1 字节处理 */
static int utf8_nbytes(const char *s, int avail)
{
	int n, i;

	if (avail <= 0)
		return 0;
	n = utf8_expected_len((unsigned char)s[0]);
	if (n > avail)
		return 1;
	for (i = 1; i < n; i++) {
		if (((unsigned char)s[i] & 0xc0) != 0x80)
			return 1;
	}
	return n;
}

/* pos 之前一个 UTF-8 字符的起始下标 */
static int utf8_prev(const char *buf, int pos)
{
	int i;

	if (pos <= 0)
		return 0;
	i = pos - 1;
	while (i > 0 && ((unsigned char)buf[i] & 0xc0) == 0x80)
		i--;
	return i;
}

static unsigned int utf8_decode(const char *s, int n)
{
	unsigned char c0 = (unsigned char)s[0];

	if (n <= 1)
		return c0;
	if (n == 2)
		return ((c0 & 0x1f) << 6) | ((unsigned char)s[1] & 0x3f);
	if (n == 3)
		return ((c0 & 0x0f) << 12) |
		       (((unsigned char)s[1] & 0x3f) << 6) |
		       ((unsigned char)s[2] & 0x3f);
	return ((c0 & 0x07) << 18) |
	       (((unsigned char)s[1] & 0x3f) << 12) |
	       (((unsigned char)s[2] & 0x3f) << 6) |
	       ((unsigned char)s[3] & 0x3f);
}

/*
 * 终端显示列宽（简化 East Asian Width）。
 * 中文等宽字符为 2，ASCII 为 1；避免按字节退格把 PS1 擦掉。
 */
static int utf8_char_width(const char *s, int n)
{
	unsigned int cp;

	if (n <= 1)
		return 1;
	cp = utf8_decode(s, n);
	if (cp >= 0x1100 && cp <= 0x115f)
		return 2;
	if (cp >= 0x2329 && cp <= 0x232a)
		return 2;
	if (cp >= 0x2e80 && cp <= 0xa4cf)
		return 2;
	if (cp >= 0xac00 && cp <= 0xd7a3)
		return 2;
	if (cp >= 0xf900 && cp <= 0xfaff)
		return 2;
	if (cp >= 0xfe10 && cp <= 0xfe19)
		return 2;
	if (cp >= 0xfe30 && cp <= 0xfe6f)
		return 2;
	if (cp >= 0xff00 && cp <= 0xff60)
		return 2;
	if (cp >= 0xffe0 && cp <= 0xffe6)
		return 2;
	if (cp >= 0x1f300 && cp <= 0x1f64f)
		return 2;
	if (cp >= 0x20000 && cp <= 0x3fffd)
		return 2;
	return 1;
}

/* buf[0 .. nbytes) 的显示列宽 */
static int str_disp_width(const char *s, int nbytes)
{
	int i = 0, w = 0, n;

	while (i < nbytes) {
		n = utf8_nbytes(s + i, nbytes - i);
		if (n <= 0)
			break;
		w += utf8_char_width(s + i, n);
		i += n;
	}
	return w;
}

/* ---------- 终端光标 / 行缓冲重绘 ---------- */

/* 光标左移 n 列（不是 n 字节） */
static void cursor_left(int n)
{
	while (n-- > 0) {
		write(1, ANSI_CUB, 1);
	}
}

/*
 * 从屏幕光标位置起，重画 buf[pos .. len)，再写 nclear 列空格清残留，
 * 最后把光标停在字节下标 stay。
 * nclear 按终端列计（宽字符删掉后须清 2 列）。
 */
static void refresh_tail(char *buf, int len, int pos, int nclear, int stay)
{
	int i;

	if (len > pos) {
		write(1, buf + pos, len - pos);
	}
	for (i = 0; i < nclear; i++) {
		write(1, " ", 1);
	}
	/* 写完后光标在 stay..len 的列宽 + nclear 之外，左移回去 */
	cursor_left(str_disp_width(buf + stay, len - stay) + nclear);
}

/* 删除 buf[pos .. pos+n)，后面内容前移 */
static void buf_delete(char *buf, int *len, int pos, int n)
{
	int i;

	if (n <= 0 || pos < 0 || pos + n > *len) {
		return;
	}
	for (i = pos; i < *len - n; i++) {
		buf[i] = buf[i + n];
	}
	*len -= n;
	buf[*len] = '\0';
}

/* 在 pos 处插入 nbytes 字节；缓冲区须留有空位 */
static void buf_insert_bytes(char *buf, int *len, int pos, const char *s,
			     int nbytes, int cap)
{
	int i;

	if (nbytes <= 0 || *len + nbytes >= cap || pos < 0 || pos > *len) {
		return;
	}
	for (i = *len - 1; i >= pos; i--) {
		buf[i + nbytes] = buf[i];
	}
	for (i = 0; i < nbytes; i++) {
		buf[pos + i] = s[i];
	}
	*len += nbytes;
	buf[*len] = '\0';
}

/* 重绘整行输入区；结束后光标在行尾 */
static void redraw_line(char *buf, int *len, int *pos, const char *text)
{
	int old_cols = str_disp_width(buf, *len);
	int new_cols;
	int nclear;

	cursor_left(str_disp_width(buf, *pos));
	strlcpy_local(buf, text, BUFSZ);
	*len = strlen(buf);
	*pos = *len;
	new_cols = str_disp_width(buf, *len);
	nclear = old_cols > new_cols ? old_cols - new_cols : 0;
	refresh_tail(buf, *len, 0, nclear, *pos);
}

/* ---------- 历史 ---------- */

/* 添加历史记录 */
static void hist_add(const char *line)
{
	int i;

	if (!line || !line[0]) {
		return;
	}
	/* 与上一条相同则不重复记 */
	if (hist_n > 0 && strcmp(history[hist_n - 1], line) == 0) {
		return;
	}

	if (hist_n < HISTMAX) {
		strlcpy_local(history[hist_n], line, BUFSZ);
		hist_n++;
		return;
	}
	/* 满则前移丢掉最旧一条 */
	for (i = 1; i < HISTMAX; i++) {
		strlcpy_local(history[i - 1], history[i], BUFSZ);
	}
	strlcpy_local(history[HISTMAX - 1], line, BUFSZ);
}

/* 编辑打断历史浏览：保存草稿并退出 viewing */
static void hist_touch(char *buf, int len, char *draft, int *viewing)
{
	if (*viewing < 0) {
		return;
	}
	buf[len] = '\0';
	strlcpy_local(draft, buf, BUFSZ);
	*viewing = -1;
}

/* ---------- 提示符 / 清屏 ---------- */

/* 类似 bash 彩色 PS1：user@host:path$（受 ansi_color 开关控制） */
static void print_prompt(void)
{
	printf("%s@%s:%s%s ",
	       C_BLUE("root"), C_BLUE("tiny"),
	       C_GREEN("/"), C_BLUE("$"));
}

/*
 * 清屏：向终端发 ANSI 序列（同 Linux clear / bash Ctrl+L）。
 * 串口终端（Xshell 等）解释序列；不经过 ansi_color 开关。
 */
static void clear_screen(void)
{
	printf("%s", ANSI_CLEAR);
}

/* ---------- readline 行编辑动作 ---------- */

/* 左箭头：左移一个 UTF-8 字符（按显示列退格） */
static void edit_left(char *buf, int *pos)
{
	int prev, w;

	if (*pos <= 0)
		return;
	prev = utf8_prev(buf, *pos);
	w = str_disp_width(buf + prev, *pos - prev);
	cursor_left(w);
	*pos = prev;
}

/* 右箭头：右移一个 UTF-8 字符（重打该字符以推进光标） */
static void edit_right(char *buf, int len, int *pos)
{
	int n;

	if (*pos >= len)
		return;
	n = utf8_nbytes(buf + *pos, len - *pos);
	write(1, buf + *pos, n);
	*pos += n;
}

/*
 * Ctrl+←：backward-word（空白分词）。
 * 先跳过左侧空白，再跳到当前/上一单词的开头。
 */
static void edit_word_left(char *buf, int *pos)
{
	int start = *pos;

	while (*pos > 0 && is_space(buf[*pos - 1])) {
		(*pos)--;
	}
	while (*pos > 0 && !is_space(buf[*pos - 1])) {
		(*pos)--;
	}
	cursor_left(str_disp_width(buf + *pos, start - *pos));
}

/*
 * Ctrl+→：forward-word（空白分词）。
 * 先跳过当前单词，再跳过其后空白，停在下一单词开头（或行尾）。
 */
static void edit_word_right(char *buf, int len, int *pos)
{
	int start = *pos;

	while (*pos < len && !is_space(buf[*pos])) {
		(*pos)++;
	}
	while (*pos < len && is_space(buf[*pos])) {
		(*pos)++;
	}
	if (*pos > start) {
		write(1, buf + start, *pos - start);
	}
}

/* Ctrl+A：光标到行首 */
static void edit_home(char *buf, int *pos)
{
	cursor_left(str_disp_width(buf, *pos));
	*pos = 0;
}

/* Ctrl+E：光标到行尾 */
static void edit_end(char *buf, int len, int *pos)
{
	if (*pos < len) {
		write(1, buf + *pos, len - *pos);
		*pos = len;
	}
}

/* Backspace：删光标前一个 UTF-8 字符（按显示列擦除） */
static void edit_backspace(char *buf, int *len, int *pos)
{
	int prev, nbytes, cols;

	if (*pos <= 0)
		return;
	prev = utf8_prev(buf, *pos);
	nbytes = *pos - prev;
	cols = str_disp_width(buf + prev, nbytes);
	*pos = prev;
	buf_delete(buf, len, *pos, nbytes);
	cursor_left(cols);
	refresh_tail(buf, *len, *pos, cols, *pos);
}

/*
 * Ctrl+W：unix-word-rubout。
 * 以空白为词界：先去掉光标前连续空白，再删掉前一个单词。
 */
static void edit_kill_word(char *buf, int *len, int *pos)
{
	int end = *pos;
	int nerase, cols;

	while (*pos > 0 && is_space(buf[*pos - 1])) {
		(*pos)--;
	}
	while (*pos > 0 && !is_space(buf[*pos - 1])) {
		(*pos)--;
	}
	nerase = end - *pos;
	if (nerase <= 0) {
		return;
	}
	cols = str_disp_width(buf + *pos, nerase);
	buf_delete(buf, len, *pos, nerase);
	cursor_left(cols);
	refresh_tail(buf, *len, *pos, cols, *pos);
}

/* Ctrl+L：清屏后重画 PS1 与编辑行，光标回到原相对位置 */
static void edit_clear_redraw(char *buf, int len, int pos)
{
	clear_screen();
	print_prompt();
	if (len > 0) {
		write(1, buf, len);
	}
	cursor_left(str_disp_width(buf + pos, len - pos));
}

/*
 * 在光标处插入一个完整 UTF-8 字符（可多字节）。
 * 必须整字写入再重绘：若按字节插入，中间态会把不完整序列与右侧 ASCII
 * 一起发给终端，解码即乱码。
 */
static void edit_insert_bytes(char *buf, int *len, int *pos, const char *s,
			      int nbytes, int cap)
{
	if (nbytes <= 0)
		return;
	buf_insert_bytes(buf, len, *pos, s, nbytes, cap);
	refresh_tail(buf, *len, *pos, 0, *pos + nbytes);
	*pos += nbytes;
}

/* 历史上翻：更旧一条；失败返回 0 */
static int hist_up(char *buf, int *len, int *pos, char *draft, int *viewing)
{
	if (hist_n == 0) {
		return 0;
	}
	if (*viewing < 0) {
		buf[*len] = '\0';
		strlcpy_local(draft, buf, BUFSZ);
		*viewing = hist_n - 1;
	} else if (*viewing > 0) {
		(*viewing)--;
	} else {
		return 0;
	}
	redraw_line(buf, len, pos, history[*viewing]);
	return 1;
}

/* 历史下翻：更新；回到草稿时 viewing=-1 */
static int hist_down(char *buf, int *len, int *pos, char *draft, int *viewing)
{
	if (*viewing < 0) {
		return 0;
	}
	if (*viewing + 1 < hist_n) {
		(*viewing)++;
		redraw_line(buf, len, pos, history[*viewing]);
	} else {
		*viewing = -1;
		redraw_line(buf, len, pos, draft);
	}
	return 1;
}

/*
 * 读一行；支持：
 *   Backspace、回车
 *   Ctrl+A / Ctrl+E —— 行首 / 行尾
 *   Ctrl+L / Ctrl+W —— 清屏 / 删词
 *   ESC [ A/B —— 上/下历史；ESC [ C/D —— 右/左移光标
 *   ESC [ 1 ; 5 C/D —— Ctrl+右/左，按单词移动（xterm/Xshell 常用）
 */
static int readline(char *buf, int n)
{
	int len = 0;		/* 当前行长度 */
	int pos = 0;		/* 光标位置 [0, len] */
	int c;
	char ch;
	/*
	 * 转义状态机：
	 *   0 — 普通字符
	 *   1 — 已读到 ESC，等待 '[' 进入 CSI
	 *   2 — 正在解析 CSI（Control Sequence Introducer）参数
	 *
	 * 方向键不是单字节，而是 CSI 序列，例如：
	 *   ESC [ D         左移一格（无参数）
	 *   ESC [ 1 ; 5 D   Ctrl+左（参数 1 与 5；5 = Ctrl 修饰）
	 */
	int esc = 0;
	/*
	 * CSI 参数解析（仅 esc==2 时使用）：
	 *   csi_params[]  — 已收下的数字参数，最多 2 个
	 *   csi_nparams   — 已收下几个参数
	 *   csi_val       — 当前正在拼的数字（多位十进制累加）
	 *   csi_in_num    — 是否已读到至少一位数字（区分「空参数」与 0）
	 *   ctrl          — 最终字节到来时：第二参数是否为 5（xterm Ctrl）
	 */
	int csi_params[2];
	int csi_nparams;
	int csi_val;
	int csi_in_num;
	int ctrl;
	char draft[BUFSZ];
	int viewing = -1;	/* -1：编辑当前行；否则 history 下标 */
	/* 凑齐一个 UTF-8 字符再插入，避免行中插入中文乱码 */
	char utf8_pending[4];
	int utf8_pend_n = 0;
	int utf8_need = 0;

	draft[0] = '\0';
	buf[0] = '\0';

	while (len + 1 < n) {
		if (read(0, &ch, 1) != 1) {
			return -1;
		}
		c = (unsigned char)ch;

		/* 控制/编辑键打断未完成的多字节序列 */
		if (c < 32 || c == KEY_DELETE) {
			utf8_pend_n = 0;
			utf8_need = 0;
		}

		/* 状态 1：刚收到 ESC，下一字节应为 '[' 才进入 CSI */
		if (esc == 1) {
			if (c == '[') {
				esc = 2;
				csi_nparams = 0;
				csi_val = 0;
				csi_in_num = 0;
			} else {
				/* 孤立的 ESC 或非 CSI 序列，丢弃 */
				esc = 0;
			}
			continue;
		}

		/*
		 * 状态 2：解析 CSI。
		 * 格式：ESC [ <参数用';分隔> <最终命令字母>
		 *   数字 → 累加进 csi_val
		 *   ';'  → 把当前数字（或空参数 0）推入 csi_params，再读下一参数
		 *   字母 → 序列结束，按字母 + 参数分发（A/B 历史，C/D 光标）
		 */
		if (esc == 2) {
			if (c >= '0' && c <= '9') {
				csi_val = csi_val * 10 + (c - '0');
				csi_in_num = 1;
				continue;
			}
			if (c == ';') {
				/* 参数分隔：空段（如 ESC[;5D）记为 0 */
				if (csi_nparams < 2) {
					csi_params[csi_nparams++] =
						csi_in_num ? csi_val : 0;
				}
				csi_val = 0;
				csi_in_num = 0;
				continue;
			}
			/* 最终命令字节：先收尾当前参数，再退出 CSI 状态 */
			esc = 0;
			if (csi_in_num && csi_nparams < 2) {
				csi_params[csi_nparams++] = csi_val;
			}
			/*
			 * xterm 修饰键约定：CSI 1 ; <mod> <C|D>
			 * mod=5 表示 Ctrl，故 Ctrl+← 为 ESC[1;5D，Ctrl+→ 为 ESC[1;5C。
			 * 无第二参数判断，避免与「CSI n D = 左移 n 格」混淆。
			 */
			ctrl = (csi_nparams >= 2 && csi_params[1] == 5);

			if (c == 'A') {		/* 上：更旧历史 */
				hist_up(buf, &len, &pos, draft, &viewing);
			} else if (c == 'B') {	/* 下：更新历史 */
				hist_down(buf, &len, &pos, draft, &viewing);
			} else if (c == 'C') {	/* 右 / Ctrl+右 */
				if (ctrl) {
					edit_word_right(buf, len, &pos);
				} else {
					edit_right(buf, len, &pos);
				}
			} else if (c == 'D') {	/* 左 / Ctrl+左 */
				if (ctrl) {
					edit_word_left(buf, &pos);
				} else {
					edit_left(buf, &pos);
				}
			}
			continue;
		}

		if (c == KEY_ESC) {
			esc = 1;
			continue;
		}
		if (c == '\r') {
			c = '\n';
		}

		if (c == KEY_BACKSPACE || c == KEY_DELETE) {
			edit_backspace(buf, &len, &pos);
			continue;
		}
		if (c == '\n') {
			/* 先移到行尾再换行，避免光标停在行中 */
			edit_end(buf, len, &pos);
			buf[len] = '\0';
			write(1, "\n", 1);
			return len;
		}
		if (c == CTRL_A) {
			edit_home(buf, &pos);
			continue;
		}
		if (c == CTRL_E) {
			edit_end(buf, len, &pos);
			continue;
		}
		if (c == CTRL_L) {
			edit_clear_redraw(buf, len, pos);
			continue;
		}
		if (c == CTRL_W) {
			edit_kill_word(buf, &len, &pos);
			hist_touch(buf, len, draft, &viewing);
			continue;
		}
		/* 不可打印字符（含控制字符；UTF-8 续字节 >= 0x80） */
		if (c < 32) {
			continue;
		}

		/*
		 * 组装完整 UTF-8 后再 edit_insert：
		 * 中文等 3 字节字符若逐字节插入并 refresh_tail，
		 * 终端会把「半截 UTF-8 + 右侧 ASCII」当成错误序列显示。
		 */
		if (utf8_pend_n == 0) {
			utf8_need = utf8_expected_len((unsigned char)c);
			utf8_pending[0] = (char)c;
			utf8_pend_n = 1;
		} else if (((unsigned char)c & 0xc0) == 0x80 &&
			   utf8_pend_n < utf8_need) {
			utf8_pending[utf8_pend_n++] = (char)c;
		} else {
			/* 续字节不合法：丢掉半截，用当前字节重新开始 */
			utf8_need = utf8_expected_len((unsigned char)c);
			utf8_pending[0] = (char)c;
			utf8_pend_n = 1;
		}
		if (utf8_pend_n < utf8_need) {
			continue;
		}
		edit_insert_bytes(buf, &len, &pos, utf8_pending, utf8_pend_n, n);
		utf8_pend_n = 0;
		utf8_need = 0;
		hist_touch(buf, len, draft, &viewing);
	}
	buf[len] = '\0';
	return len;
}

/* ---------- builtins（argc/argv；返回 0 表示成功） ---------- */

static int cmd_help(int argc, char **argv);
static int cmd_echo(int argc, char **argv);
static int cmd_cat(int argc, char **argv);
static int cmd_ls(int argc, char **argv);
static int cmd_pid(int argc, char **argv);
static int cmd_color(int argc, char **argv);
static int cmd_clear(int argc, char **argv);
static int cmd_exit(int argc, char **argv);

struct builtin {
	const char *name;
	const char *usage;	/* help 里显示的说明 */
	int (*func)(int argc, char **argv);
};

static const struct builtin builtins[] = {
	{ "help",  "          show this message",     cmd_help },
	{ "echo",  " [args]   print arguments",       cmd_echo },
	{ "cat",   " <path>   print file",            cmd_cat },
	{ "ls",    " [path]   list directory",        cmd_ls },
	{ "pid",   "          print shell pid",       cmd_pid },
	{ "color", " [on|off] color switch",          cmd_color },
	{ "clear", "          clear screen",          cmd_clear },
	{ "exit",  "          leave shell",           cmd_exit },
};

/* 运行时字符串上色（C_* 仅适用于字面量） */
static void print_colored(const char *attr, const char *s)
{
	if (ansi_color) {
		printf("%s%s%s", attr, s, ANSI_RESET);
	} else {
		printf("%s", s);
	}
}

static int cmd_help(int argc, char **argv)
{
	unsigned int i;

	(void)argc;
	(void)argv;
	printf("%s\n", C_CYAN("Tiny-OS shell"));
	printf("%s\n", C_BOLD("Built-in:"));
	for (i = 0; i < NELEM(builtins); i++) {
		printf("  ");
		print_colored(ANSI_FG_BGREEN, builtins[i].name);
		printf("%s\n", builtins[i].usage);
	}
	printf("%s\n", C_BOLD("Keys:"));
	printf("  %s       command history\n", C_YELLOW("up/down"));
	printf("  %s   move cursor\n", C_YELLOW("left/right"));
	printf("  %s  move by word\n", C_YELLOW("Ctrl+left/right"));
	printf("  %s         beginning of line\n", C_YELLOW("Ctrl+A"));
	printf("  %s         end of line\n", C_YELLOW("Ctrl+E"));
	printf("  %s         clear screen\n", C_YELLOW("Ctrl+L"));
	printf("  %s         delete previous word\n", C_YELLOW("Ctrl+W"));
	printf("%s sh\n", C_BOLD("Programs:"));
	return 0;
}

static int cmd_echo(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (i > 1) {
			printf(" ");
		}
		printf("%s", argv[i]);
	}
	printf("\n");
	return 0;
}

static int cmd_ls(int argc, char **argv)
{
	const char *path;
	struct dirent de;
	struct stat st;
	int fd, n;

	path = (argc >= 2) ? argv[1] : "/";
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		printf("%s cannot open %s\n", C_RED("ls:"), path);
		return 1;
	}
	if (fstat(fd, &st) < 0) {
		printf("%s fstat failed\n", C_RED("ls:"));
		close(fd);
		return 1;
	}
	if (!S_ISDIR(st.st_mode)) {
		/* 与常见 ls 类似：对文件只打印路径 */
		printf("%s\n", path);
		close(fd);
		return 0;
	}
	while ((n = read(fd, &de, sizeof(de))) > 0) {
		if (n < (int)sizeof(de) || de.d_reclen < sizeof(de)) {
			printf("%s short dirent\n", C_RED("ls:"));
			close(fd);
			return 1;
		}
		if (de.d_type == DT_DIR) {
			print_colored(ANSI_FG_BBLUE, de.d_name);
			printf("/\n");
		} else if (de.d_type == DT_CHR || de.d_type == DT_BLK) {
			print_colored(ANSI_FG_BYELLOW, de.d_name);
			printf("\n");
		} else {
			printf("%s\n", de.d_name);
		}
	}
	if (n < 0) {
		printf("%s read error\n", C_RED("ls:"));
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}

static int cmd_cat(int argc, char **argv)
{
	char buf[64];
	int fd, n;

	if (argc < 2) {
		printf("%s cat <path>\n", C_RED("usage:"));
		return 1;
	}
	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		printf("%s cannot open %s\n", C_RED("cat:"), argv[1]);
		return 1;
	}
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		write(1, buf, n);
	}
	close(fd);
	return 0;
}

static int cmd_pid(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	printf("%d\n", getpid());
	return 0;
}

static int cmd_exit(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	exit(0);
}

/* color / color on / color off —— 开关 ANSI 颜色 */
static int cmd_color(int argc, char **argv)
{
	if (argc == 1) {
		printf("color is %s\n", ansi_color ? "on" : "off");
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "on") == 0) {
		ansi_color = 1;
		printf("color on\n");
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "off") == 0) {
		ansi_color = 0;
		printf("color off\n");
		return 0;
	}
	printf("%s color [on|off]\n", C_RED("usage:"));
	return 1;
}

/* clear —— 同 Linux clear(1) / bash Ctrl+L */
static int cmd_clear(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	clear_screen();
	return 0;
}

/* 执行内置命令；返回 1 表示已处理 */
static int run_builtin(int argc, char **argv)
{
	unsigned int i;

	if (argc <= 0) {
		return 1;
	}

	for (i = 0; i < NELEM(builtins); i++) {
		if (strcmp(argv[0], builtins[i].name) == 0) {
			builtins[i].func(argc, argv);
			return 1;
		}
	}
	return 0;
}

/* 执行外部命令 */
static void run_external(const char *cmd)
{
	int pid;
	int status;

	pid = fork();
	if (pid == 0) {
		execve(cmd, 0, 0);
		printf("%s exec %s failed\n", C_RED("sh:"), cmd);
		exit(1);
	}
	if (pid < 0) {
		printf("%s fork failed\n", C_RED("sh:"));
		return;
	}
	waitpid(pid, &status, 0);
}

static void run_line(char *line)
{
	char *argv[MAXARGV];
	int argc;

	argc = split_argv(line, argv, MAXARGV);
	if (argc == 0) {
		return;
	}
	if (run_builtin(argc, argv)) {
		return;
	}
	run_external(argv[0]);
}

void print_banner(void)
{
	printf("\n");
	printf("%s", C_CYAN("========================================\n"));
	printf("%s", C_GREEN(
		"    _______                ____  _____\n"
		"   /_  __(_)___  __  __   / __ \\/ ___/\n"
		"    / / / / __ \\/ / / /  / / / /\\__ \\\n"
		"   / / / / / / / /_/ /  / /_/ /___/ /\n"
		"  /_/ /_/_/ /_/\\__, /   \\____//____/\n"
		"              /____/\n"));
	printf("\n");
	printf("        Welcome to Tiny-OS shell\n");
	printf("        type 'help' for commands\n");
	printf("\n");
	printf("%s", C_CYAN("========================================\n"));
	printf("\n");
}

int main(void)
{
	char line[BUFSZ];

	/* print_banner(); */
	for (;;) {
		print_prompt();
		if (readline(line, sizeof(line)) < 0) {
			/* 串口暂无输入时不退出，避免 init 反复 fork */
			sleep(1);
			continue;
		}
		hist_add(line);
		run_line(line);
	}
}
