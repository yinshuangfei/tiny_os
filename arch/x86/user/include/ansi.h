/*
 * 终端 ANSI 颜色（与 Linux 控制台 / bash PS1 常用序列一致）。
 * 串口本身无颜色；对端终端解释这些转义序列。
 *
 * 全局开关 ansi_color：1 输出转义序列，0 输出纯文本。
 * C_* 为运行时表达式，须用 %s 打印，例如：
 *   printf("%s done\n", C_GREEN("ok"));
 *   printf("%s %s\n", C_RED("err:"), msg);
 */
#ifndef __USER_ANSI_H__
#define __USER_ANSI_H__

#define ANSI_ESC		"\033["		/* 转义序列开始 */
#define ANSI_RESET		ANSI_ESC "0m"		/* 重置 */
#define ANSI_BOLD		ANSI_ESC "1m"		/* 粗体 */
#define ANSI_DIM		ANSI_ESC "2m"		/* 淡化 */

/* 前景色 */
#define ANSI_FG_BLACK		ANSI_ESC "30m"		/* 黑色 */
#define ANSI_FG_RED		ANSI_ESC "31m"		/* 红色 */
#define ANSI_FG_GREEN		ANSI_ESC "32m"		/* 绿色 */
#define ANSI_FG_YELLOW		ANSI_ESC "33m"		/* 黄色 */
#define ANSI_FG_BLUE		ANSI_ESC "34m"		/* 蓝色 */
#define ANSI_FG_MAGENTA		ANSI_ESC "35m"		/* 洋红色 */
#define ANSI_FG_CYAN		ANSI_ESC "36m"		/* 青色 */
#define ANSI_FG_WHITE		ANSI_ESC "37m"		/* 白色 */

/* 亮色前景 */
#define ANSI_FG_BRED		ANSI_ESC "1;31m"	/* 红色 */
#define ANSI_FG_BGREEN		ANSI_ESC "1;32m"	/* 绿色 */
#define ANSI_FG_BYELLOW		ANSI_ESC "1;33m"	/* 黄色 */
#define ANSI_FG_BBLUE		ANSI_ESC "1;34m"	/* 蓝色 */
#define ANSI_FG_BMAGENTA	ANSI_ESC "1;35m"	/* 洋红色 */
#define ANSI_FG_BCYAN		ANSI_ESC "1;36m"	/* 青色 */
#define ANSI_FG_BWHITE		ANSI_ESC "1;37m"	/* 白色 */

/* 背景色（少用） */
#define ANSI_BG_RED		ANSI_ESC "41m"		/* 红色 */
#define ANSI_BG_GREEN		ANSI_ESC "42m"		/* 绿色 */

/* 1=开（默认），0=关；由 color 内置命令等修改 */
extern int ansi_color;

/*
 * attr + 字面量 + 复位；关闭颜色时退化为纯文本。
 * 结果是运行时表达式（非字面量），不能与相邻字符串拼接。
 */
#define C(attr, s)		(ansi_color ? attr s ANSI_RESET : s)

#define C_BOLD(s)		C(ANSI_BOLD, s)
#define C_DIM(s)		C(ANSI_DIM, s)

#define C_RED(s)		C(ANSI_FG_BRED, s)
#define C_GREEN(s)		C(ANSI_FG_BGREEN, s)
#define C_YELLOW(s)		C(ANSI_FG_BYELLOW, s)
#define C_BLUE(s)		C(ANSI_FG_BBLUE, s)
#define C_MAGENTA(s)		C(ANSI_FG_BMAGENTA, s)
#define C_CYAN(s)		C(ANSI_FG_BCYAN, s)
#define C_WHITE(s)		C(ANSI_FG_BWHITE, s)

#endif
