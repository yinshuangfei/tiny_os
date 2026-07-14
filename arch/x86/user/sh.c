/*
 * 简易用户态 shell：从 stdin（串口）读命令，内置 help/echo/pid/cat/exit/color/clear，
 * 方向键：上下历史，左右移动光标（可在光标处插入）；
 * Ctrl+←/→ 按单词移动；Ctrl+A/E 行首行尾，Ctrl+L 清屏，Ctrl+W 删词。
 * 其它名称经 fork + execve + waitpid 执行。
 * 提示符 / 帮助 / 错误使用 ANSI 颜色（对端终端解释，同 Linux）；
 * 可用内置命令 color on|off 开关。
 *
 * 结构贴近 bash 教学版：PS1 提示符、行内分词为 argv、内置命令表分发。
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

/* ---------- 终端光标 / 行缓冲重绘 ---------- */

/* 光标左移 n 格 */
static void cursor_left(int n)
{
	while (n-- > 0) {
		write(1, ANSI_CUB, 1);
	}
}

/*
 * 从屏幕光标位置起，重画 buf[pos .. len)，再写 nclear 个空格清残留，
 * 最后把光标停在 stay（相对 buf 起点）。
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
	/* 写完后光标在 len+nclear，左移到 stay */
	cursor_left(len + nclear - stay);
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

/* 在 pos 处插入一字符；缓冲区须留有空位 */
static void buf_insert(char *buf, int *len, int pos, char c, int cap)
{
	int i;

	if (*len + 1 >= cap || pos < 0 || pos > *len) {
		return;
	}
	for (i = *len; i > pos; i--) {
		buf[i] = buf[i - 1];
	}
	buf[pos] = c;
	(*len)++;
	buf[*len] = '\0';
}

/* 重绘整行输入区；结束后光标在行尾 */
static void redraw_line(char *buf, int *len, int *pos, const char *text)
{
	int oldlen = *len;
	int nclear;

	cursor_left(*pos);
	strlcpy_local(buf, text, BUFSZ);
	*len = strlen(buf);
	*pos = *len;
	nclear = oldlen > *len ? oldlen - *len : 0;
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

/* 左箭头 / 光标左移一格 */
static void edit_left(int *pos)
{
	if (*pos > 0) {
		cursor_left(1);
		(*pos)--;
	}
}

/* 右箭头 / 光标右移一格（重打该字符以推进光标） */
static void edit_right(char *buf, int len, int *pos)
{
	if (*pos < len) {
		write(1, &buf[*pos], 1);
		(*pos)++;
	}
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
	cursor_left(start - *pos);
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
static void edit_home(int *pos)
{
	cursor_left(*pos);
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

/* Backspace：删光标前一字符 */
static void edit_backspace(char *buf, int *len, int *pos)
{
	if (*pos <= 0) {
		return;
	}
	(*pos)--;
	buf_delete(buf, len, *pos, 1);
	write(1, ANSI_CUB, 1);
	refresh_tail(buf, *len, *pos, 1, *pos);
}

/*
 * Ctrl+W：unix-word-rubout。
 * 以空白为词界：先去掉光标前连续空白，再删掉前一个单词。
 */
static void edit_kill_word(char *buf, int *len, int *pos)
{
	int end = *pos;
	int nerase;

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
	buf_delete(buf, len, *pos, nerase);
	cursor_left(nerase);
	refresh_tail(buf, *len, *pos, nerase, *pos);
}

/* Ctrl+L：清屏后重画 PS1 与编辑行，光标回到原相对位置 */
static void edit_clear_redraw(char *buf, int len, int pos)
{
	clear_screen();
	print_prompt();
	if (len > 0) {
		write(1, buf, len);
	}
	cursor_left(len - pos);
}

/* 在光标处插入可打印字符 */
static void edit_insert(char *buf, int *len, int *pos, char c, int cap)
{
	buf_insert(buf, len, *pos, c, cap);
	refresh_tail(buf, *len, *pos, 0, *pos + 1);
	(*pos)++;
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

	draft[0] = '\0';
	buf[0] = '\0';

	while (len + 1 < n) {
		if (read(0, &ch, 1) != 1) {
			return -1;
		}
		c = (unsigned char)ch;

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
					edit_left(&pos);
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
			edit_home(&pos);
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
		/* 不可打印字符 */
		if (c < 32) {
			continue;
		}

		edit_insert(buf, &len, &pos, (char)c, n);
		hist_touch(buf, len, draft, &viewing);
	}
	buf[len] = '\0';
	return len;
}

/* ---------- builtins（argc/argv；返回 0 表示成功） ---------- */

static int cmd_help(int argc, char **argv);
static int cmd_echo(int argc, char **argv);
static int cmd_cat(int argc, char **argv);
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
			break;
		}
		hist_add(line);
		run_line(line);
	}
	exit(0);
}
