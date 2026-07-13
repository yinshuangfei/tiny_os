/*
 * 简易用户态 shell：从 stdin（串口）读命令，内置 help/pid/exit，
 * 其它名称经 fork + execve + waitpid 执行（如 sh）。
 */
#include "user.h"

#define BUFSZ 128

static int is_space(char c)
{
	return c == ' ' || c == '\t';
}

static char *skipsp(char *s)
{
	while (*s && is_space(*s))
		s++;
	return s;
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

/* 取行首第一个单词到 cmd */
static void firstword(char *line, char *cmd, int cmdlen)
{
	char *p = skipsp(line);
	int i = 0;

	while (p[i] && !is_space(p[i]) && i + 1 < cmdlen) {
		cmd[i] = p[i];
		i++;
	}
	cmd[i] = '\0';
}

static int readline(char *buf, int n)
{
	int i = 0;
	int c;

	while (i + 1 < n) {
		if (read(0, &buf[i], 1) != 1)
			return -1;
		c = (unsigned char)buf[i];
		if (c == '\r')
			c = '\n';
		if (c == '\b' || c == 127) {
			if (i > 0) {
				i--;
				write(1, "\b \b", 3);
			}
			continue;
		}
		if (c == '\n') {
			buf[i] = '\0';
			write(1, "\n", 1);
			return i;
		}
		buf[i] = (char)c;
		i++;
		write(1, &buf[i - 1], 1);
	}
	buf[i] = '\0';
	return i;
}

/* 执行内置命令 */
static int run_builtin(const char *cmd)
{
	if (*cmd == '\0')
		return 1;
	if (streq(cmd, "help")) {
		printf("commands: help, pid, exit\n");
		printf("programs: sh\n");
		return 1;
	}
	if (streq(cmd, "pid")) {
		printf("%d\n", getpid());
		return 1;
	}
	if (streq(cmd, "exit")) {
		exit(0);
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
		printf("sh: exec %s failed\n", cmd);
		exit(1);
	}
	if (pid < 0) {
		printf("sh: fork failed\n");
		return;
	}
	waitpid(pid, &status, 0);
}

static void run_cmd(char *line)
{
	char cmd[BUFSZ];

	firstword(line, cmd, sizeof(cmd));
	if (!run_builtin(cmd))
		run_external(cmd);
}

void print_banner(void)
{
	printf("\n");
	printf("========================================\n");
	printf("    _______                ____  _____\n");
	printf("   /_  __(_)___  __  __   / __ \\/ ___/\n");
	printf("    / / / / __ \\/ / / /  / / / /\\__ \\\n");
	printf("   / / / / / / / /_/ /  / /_/ /___/ /\n");
	printf("  /_/ /_/_/ /_/\\__, /   \\____//____/\n");
	printf("              /____/\n");
	printf("\n");
	printf("        Welcome to Tiny-OS shell\n");
	printf("        type 'help' for commands\n");
	printf("\n");
	printf("========================================\n");
	printf("\n");
}

int main(void)
{
	char line[BUFSZ];

	// print_banner();
	for (;;) {
		printf("$ ");
		if (readline(line, sizeof(line)) < 0)
			break;
		run_cmd(line);
	}
	exit(0);
}
