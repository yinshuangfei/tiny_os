/*
 * 精简 termios（教学用）：只暴露行规程相关的 c_lflag。
 * 经 ioctl(TCGETS/TCSETS) 作用于 /dev/console。
 */
#ifndef __USER_TERMIOS_H__
#define __USER_TERMIOS_H__

/* c_lflag */
#define ISIG	0x0001	/* 响应 Ctrl+C → SIGINT */
#define ICANON	0x0002	/* 规范模式（行缓冲 + 行编辑） */
#define ECHO	0x0008	/* 回显 */

/* ioctl 请求号（本内核私有，非 Linux 数值） */
#define TCGETS	1	/* 获取终端属性 */
#define TCSETS	2	/* 设置终端属性 */

/* tcsetattr 可选动作（本内核忽略，仅占位） */
#define TCSANOW	0	/* 立即生效 */

struct termios {
	unsigned int c_lflag;	/* local modes 行规程 */
};

#ifndef __ASSEMBLER__
int ioctl(int fd, unsigned int request, void *arg);
int tcgetattr(int fd, struct termios *tio);
int tcsetattr(int fd, int optional_actions, const struct termios *tio);
#endif

#endif
