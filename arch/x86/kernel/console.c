/*
 * 系统控制台：注册多个输出后端；键盘/串口输入汇入同一行规程缓冲。
 *
 * 行规程（精简 termios）：
 *   ICANON — 规范模式：行编辑，读进程只在收到 '\\n' 后才能读到整行
 *   ECHO   — 回显输入（含退格擦除）
 *   ISIG   — Ctrl+C → SIGINT（始终回显 "^C\\n"，与 bash 一致）
 *
 * 默认 ICANON|ECHO|ISIG。shell readline 经 ioctl 关掉 ICANON|ECHO（raw）。
 * 前台 pid 由 wait 路径维护。
 */
#include "types.h"
#include "defs.h"
#include "console.h"
#include "vga.h"
#include "lock/spinlock.h"
#include "proc.h"
#include "lock/proc_lock.h"
#include "ipc/signal.h"
#include "mm/vm.h"
#include "driver/chrdev.h"
#include "fs/vfs.h"
#include "major.h"

#define CONS_BUF	256
#define ASCII_ETX	0x03	/* Ctrl+C */
#define ASCII_EOT	0x04	/* End of Transmission, Ctrl+D */
#define ASCII_BS	0x08	/* Backspace, Ctrl+H */
#define ASCII_NAK	0x15	/* Negative Acknowledge, Ctrl+U */
#define ASCII_DEL	0x7f	/* Delete */

/* 与 termios.c_lflag 同值 */
#define ISIG	CONS_ISIG	/* 启用信号处理 */
#define ICANON	CONS_ICANON	/* 启用规范模式 */
#define ECHO	CONS_ECHO	/* 启用回显 */

/* ioctl 请求号（本内核私有，非 Linux 数值） */
#define TCGETS	CONS_TCGETS	/* 获取终端属性 */
#define TCSETS	CONS_TCSETS	/* 设置终端属性 */

struct termios {
	unsigned int c_lflag;
};

/* 全局控制台链表 */
static struct console *consoles;

static struct spinlock cons_lock;	/* 输入缓冲锁 */
static char cons_buf[CONS_BUF];		/* 输入缓冲 */
/* 绝对下标（取模后索引缓冲），同 xv6：r 已读，w 可读上限，e 编辑端 */
static uint cons_r, cons_w, cons_e;
static char cons_chan;			/* 等待通道 */
static int cons_fg_pid;			/* 前台进程：Ctrl+C → SIGINT */
static unsigned int cons_lflag;		/* ISIG | ICANON | ECHO */

/* ---------- ttyS0：串口 ---------- */
static void uart_console_write(struct console *con, const char *s,
			       unsigned int count)
{
	unsigned int i;

	(void)con;
	for (i = 0; i < count; i++)
		uart_putc(s[i]);
}

static struct console uart_console = {
	.name = "ttyS0",
	.write = uart_console_write,
};

/* ---------- tty0：VGA 文本 ---------- */
static void vga_console_write(struct console *con, const char *s,
			      unsigned int count)
{
	unsigned int i;

	(void)con;
	for (i = 0; i < count; i++)
		vga_putc(s[i]);
}

static struct console vga_console = {
	.name = "tty0",
	.write = vga_console_write,
};

void register_console(struct console *con)
{
	if (!con || !con->write)
		return;
	con->next = consoles;
	consoles = con;
}

void console_write(const char *s, unsigned int n)
{
	struct console *con;

	if (!s || n == 0)
		return;

	for (con = consoles; con; con = con->next)
		con->write(con, s, n);
}

void consputc(int c)
{
	char ch = (char)c;

	console_write(&ch, 1);
}

/* 回显可打印字符与换行；控制字符不回显（Ctrl+C 单独处理） */
static void cons_echo_char(int c)
{
	if (c == '\n' || (c >= 0x20 && c < 0x7f))
		consputc(c);
}

/* 回显退格：光标回退、擦除、再回退（串口终端需要；VGA 的 \\b 已清格） */
static void cons_echo_erase(void)
{
	console_write("\b \b", 3);
}

void console_set_fg(int pid)
{
	acquire(&cons_lock);
	cons_fg_pid = pid;
	release(&cons_lock);
}

int console_get_fg(void)
{
	int pid;

	acquire(&cons_lock);
	pid = cons_fg_pid;
	release(&cons_lock);
	return pid;
}

static int cons_empty(void)
{
	return cons_r == cons_w;
}

static int cons_input_full(void)
{
	return cons_e - cons_r >= CONS_BUF;
}

static void cons_flush_input(void)
{
	cons_r = cons_w = cons_e = 0;
}

static int cons_cansleep(void)
{
	struct proc *p = myproc();

	return p && p->state == RUNNING && mycpu()->noff == 0;
}

static void sleep_chan(void *chan, struct spinlock *lk)
{
	struct proc *p = myproc();

	if (!holding(lk))
		panic("console sleep_chan");
	if (!chan)
		panic("console sleep_chan: null");

	acquire(&proc_lock);
	release(lk);
	p->chan = chan;
	p->wakeup_tick = 0;
	p->state = SLEEPING;
	release(&proc_lock);
	sched();
	acquire(lk);
}

/*
 * 中断路径投递一个输入字符（键盘 / 串口）。
 * 在锁内做行规程；回显时不持锁调用 console_write（避免与输出路径纠缠）。
 */
void console_intr(int c)
{
	int fg;
	int do_wakeup = 0;
	unsigned int lflag;

	c &= 0xff;
	acquire(&cons_lock);
	lflag = cons_lflag;

	/* ISIG：Ctrl+C → 回显 ^C、清输入、SIGINT */
	if ((lflag & ISIG) && c == ASCII_ETX) {
		fg = cons_fg_pid;
		cons_flush_input();
		release(&cons_lock);
		console_write("^C\n", 3);
		if (fg > 0)
			signal_send(fg, SIGINT);
		wakeup(&cons_chan);
		return;
	}

	if (lflag & ICANON) {
		switch (c) {
		case ASCII_BS:
		case ASCII_DEL:
			if (cons_e != cons_w) {
				cons_e--;
				release(&cons_lock);
				if (lflag & ECHO)
					cons_echo_erase();
				acquire(&cons_lock);
			}
			break;
		case ASCII_NAK:	/* Ctrl+U：删整行（未提交部分） */
			while (cons_e != cons_w) {
				cons_e--;
				release(&cons_lock);
				if (lflag & ECHO)
					cons_echo_erase();
				acquire(&cons_lock);
			}
			break;
		default:
			if (c == 0 || cons_input_full())
				break;
			if (c == '\r')
				c = '\n';
			cons_buf[cons_e % CONS_BUF] = (char)c;
			cons_e++;
			release(&cons_lock);
			if (lflag & ECHO)
				cons_echo_char(c);
			acquire(&cons_lock);
			/*
			 * 整行就绪、Ctrl+D（EOF 标记）或缓冲满：
			 * 提交给读端（推进 w）。
			 */
			if (c == '\n' || c == ASCII_EOT ||
			    cons_e - cons_r == CONS_BUF) {
				cons_w = cons_e;
				do_wakeup = 1;
			}
			break;
		}
	} else {
		/* raw：逐字符可读；可选回显 */
		if (c == 0 || cons_input_full()) {
			release(&cons_lock);
			return;
		}
		if (c == '\r')
			c = '\n';
		cons_buf[cons_e % CONS_BUF] = (char)c;
		cons_e++;
		cons_w = cons_e;
		do_wakeup = 1;
		release(&cons_lock);
		if (lflag & ECHO)
			cons_echo_char(c);
		acquire(&cons_lock);
	}

	release(&cons_lock);
	if (do_wakeup)
		wakeup(&cons_chan);
}

/*
 * 读一个已提交的输入字符。信号打断返回 -1。
 * 规范模式下 Ctrl+D 作为行内 EOF：若单独成「空行」则对读端返回 -1 表示 EOF
 *（此处用返回 -1；file 层可区分；简单起见与打断相同，shell 不依赖）。
 */
int console_getc(void)
{
	int c;
	struct proc *p;

	if (!cons_cansleep()) {
		while (cons_empty())
			;
		acquire(&cons_lock);
		c = cons_buf[cons_r++ % CONS_BUF] & 0xff;
		release(&cons_lock);
		return c;
	}

	p = myproc();
	acquire(&cons_lock);
	while (cons_empty()) {
		if (signal_can_interrupt(p)) {
			release(&cons_lock);
			return -1;
		}
		sleep_chan(&cons_chan, &cons_lock);
		if (signal_can_interrupt(p)) {
			release(&cons_lock);
			return -1;
		}
	}
	c = cons_buf[cons_r++ % CONS_BUF] & 0xff;
	/* 规范模式：Ctrl+D → EOF（不把 0x04 交给用户） */
	if ((cons_lflag & ICANON) && c == ASCII_EOT) {
		release(&cons_lock);
		return -2;	/* EOF */
	}
	release(&cons_lock);
	return c;
}

int console_is_canon(void)
{
	unsigned int f;

	acquire(&cons_lock);
	f = cons_lflag;
	release(&cons_lock);
	return (f & ICANON) != 0;
}

/*
 * /dev/console ioctl：TCGETS / TCSETS（仅 c_lflag）。
 * 切换模式时清空输入缓冲，避免 raw/cooked 字符泄漏。
 */
int console_ioctl(unsigned int req, uint uarg)
{
	struct proc *p = myproc();
	struct termios tio;

	if (!p || !proc_pagetable(p))
		return -1;

	switch (req) {
	case TCGETS:
		acquire(&cons_lock);
		tio.c_lflag = cons_lflag;
		release(&cons_lock);
		if (copyout(proc_pagetable(p), uarg, &tio, sizeof(tio)) < 0)
			return -1;
		return 0;
	case TCSETS:
		if (copyin(proc_pagetable(p), &tio, uarg, sizeof(tio)) < 0)
			return -1;
		acquire(&cons_lock);
		cons_lflag = tio.c_lflag & (ISIG | ICANON | ECHO);
		cons_flush_input();
		release(&cons_lock);
		wakeup(&cons_chan);
		return 0;
	default:
		return -1;
	}
}

/*
 * /dev/console 的 file_operations（经 register_chrdev 挂到 CONSOLE_MAJOR）。
 */
static int console_fop_read(struct file *f, char *dst, int n)
{
	int i, c;
	int canon;

	(void)f;
	canon = console_is_canon();
	for (i = 0; i < n; i++) {
		c = console_getc();
		if (c == -2)
			return i;
		if (c < 0)
			return i == 0 ? -1 : i;
		dst[i] = (char)c;
		if (canon && c == '\n')
			return i + 1;
	}
	return n;
}

static int console_fop_write(struct file *f, char *src, int n)
{
	(void)f;
	if (n > 0)
		console_write(src, (unsigned int)n);
	return n;
}

static int console_fop_ioctl(struct file *f, unsigned int req, unsigned int arg)
{
	(void)f;
	return console_ioctl(req, arg);
}

static const struct file_operations console_fops = {
	.read = console_fop_read,
	.write = console_fop_write,
	.ioctl = console_fop_ioctl,
};

void console_init(void)
{
	initlock(&cons_lock, "console");
	cons_r = cons_w = cons_e = 0;
	cons_fg_pid = 0;
	cons_lflag = ISIG | ICANON | ECHO;

	/* 仅注册 printk 输出后端；/dev/console 见 console_register_device() */
	register_console(&uart_console);
	register_console(&vga_console);
}

/*
 * 对齐 Linux：早期 console_init 只服务 printk；
 * chrdev 子系统就绪后再 register_chrdev（类似 tty/console 驱动挂接）。
 */
void console_register_device(void)
{
	if (register_chrdev(CONSOLE_MAJOR, "console", &console_fops) < 0)
		panic("console: register_chrdev");
}
