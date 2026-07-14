/*
 * 简易用户态 shell：从 stdin（串口）读命令，内置 help/echo/pid/cat/exit/color，
 * 上下方向键浏览历史；其它名称经 fork + execve + waitpid 执行。
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

/* 擦除 n 个字符 */
static void erase_chars(int n)
{
	/**
	\b ：退格符（Backspace）。光标向左移动一格，回到上一个字符的位置。
	' '：一个空格字符。它被打印在刚才光标所在的位置，从而在视觉上“覆盖”或“擦除”了原来的字符。
	\b ：再次退格。光标向左移动一格，回到空格之前的位置
	*/
	while (n-- > 0) {
		write(1, "\b \b", 3);
	}
}

/* 重绘行 */
static void redraw_line(char *buf, int *len, const char *text)
{
	erase_chars(*len);
	strlcpy_local(buf, text, BUFSZ);
	*len = strlen(buf);
	if (*len > 0) {
		write(1, buf, *len);
	}
}

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

/*
 * 读一行；支持：
 *   Backspace、回车
 *   ESC [ A / ESC [ B —— 上/下历史
 */
static int readline(char *buf, int n)
{
	int i = 0;
	int c;
	int esc = 0;		/* 0 普通；1 已见 ESC；2 已见 ESC [ */
	char draft[BUFSZ];
	int viewing = -1;	/* -1：编辑当前行；否则 history 下标 */

	draft[0] = '\0';
	buf[0] = '\0';

	while (i + 1 < n) {
		if (read(0, &buf[i], 1) != 1) {
			return -1;
		}

		c = (unsigned char)buf[i];

		if (esc == 1) {
			if (c == '[') {
				esc = 2;
				continue;
			}
			esc = 0;
			/* 孤立的 ESC 字符，忽略 */
			continue;
		}
		if (esc == 2) {
			esc = 0;
			if (c == 'A') {		/* 上：更旧 */
				if (hist_n == 0) {
					continue;
				}
				if (viewing < 0) {
					buf[i] = '\0';
					strlcpy_local(draft, buf, BUFSZ);
					viewing = hist_n - 1;
				} else if (viewing > 0) {
					viewing--;
				} else {
					continue;
				}
				redraw_line(buf, &i, history[viewing]);
				continue;
			}
			if (c == 'B') {		/* 下：更新 */
				if (viewing < 0) {
					continue;
				}
				if (viewing + 1 < hist_n) {
					viewing++;
					redraw_line(buf, &i, history[viewing]);
				} else {
					viewing = -1;
					redraw_line(buf, &i, draft);
				}
				continue;
			}
			continue;
		}

		if (c == 0x1b) {	/* ESC */
			esc = 1;
			continue;
		}
		if (c == '\r') {
			c = '\n';
		}
		/* 退格 (\b = 0x08) 或删除 (Delete = 0x7f) */
		if (c == '\b' || c == 127) {
			if (i > 0) {
				i--;
				write(1, "\b \b", 3);
				buf[i] = '\0';
			}
			continue;
		}
		if (c == '\n') {
			buf[i] = '\0';
			write(1, "\n", 1);
			return i;
		}
		/* 不可打印字符 */
		if (c < 32) {
			continue;
		}

		buf[i] = (char)c;
		i++;
		buf[i] = '\0';
		write(1, &buf[i - 1], 1);

		/* 正在改当前行时，历史浏览位置失效为“草稿” */
		if (viewing >= 0) {
			strlcpy_local(draft, buf, BUFSZ);
			viewing = -1;
		}
	}
	buf[i] = '\0';
	return i;
}

/* ---------- builtins（argc/argv；返回 0 表示成功） ---------- */

static int cmd_help(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	printf("%s\n", C_CYAN("Tiny-OS shell"));
	printf("%s\n", C_BOLD("Built-in:"));
	printf("  %s           show this message\n", C_GREEN("help"));
	printf("  %s [args]    print arguments\n", C_GREEN("echo"));
	printf("  %s <path>     print file\n", C_GREEN("cat"));
	printf("  %s            print shell pid\n", C_GREEN("pid"));
	printf("  %s [on|off]   color switch\n", C_GREEN("color"));
	printf("  %s           leave shell\n", C_GREEN("exit"));
	printf("%s\n", C_BOLD("Keys:"));
	printf("  %s       command history\n", C_YELLOW("up/down"));
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

struct builtin {
	const char *name;
	int (*func)(int argc, char **argv);
};

static const struct builtin builtins[] = {
	{ "help",  cmd_help },
	{ "echo",  cmd_echo },
	{ "cat",   cmd_cat },
	{ "pid",   cmd_pid },
	{ "color", cmd_color },
	{ "exit",  cmd_exit },
};

/* 执行内置命令；返回 1 表示已处理 */
static int run_builtin(int argc, char **argv)
{
	unsigned int i;

	if (argc <= 0) {
		return 1;
	}

	for (i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
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

/* 类似 bash 彩色 PS1：user@host:path$（受 ansi_color 开关控制） */
static void print_prompt(void)
{
	printf("%s@%s:%s%s ",
	       C_GREEN("root"), C_GREEN("tiny"),
	       C_BLUE("/"), C_GREEN("$"));
}

int main(void)
{
	char line[BUFSZ];

	// print_banner();
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
