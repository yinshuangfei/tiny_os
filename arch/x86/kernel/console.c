/*
 * 系统控制台：注册多个输出后端；键盘/串口输入汇入同一缓冲。
 * Ctrl+C（0x03）不入缓冲，向前台进程发送 SIGINT。
 *
 * 前台 pid 由 wait 路径维护：
 *   父进程阻塞在 wait 时 → 前台 = 子进程；
 *   wait 返回后 → 前台交回父进程。
 */
#include "types.h"
#include "defs.h"
#include "console.h"
#include "vga.h"
#include "lock/spinlock.h"
#include "proc.h"
#include "lock/proc_lock.h"
#include "ipc/signal.h"

#define CONS_BUF	256
#define ASCII_ETX	0x03	/* Ctrl+C */

/* 全局控制台链表 */
static struct console *consoles;

static struct spinlock cons_lock;	/* 输入缓冲锁 */
static char cons_buf[CONS_BUF];		/* 输入缓冲 */
static uint cons_r, cons_w;		/* 读写指针 */
static char cons_chan;			/* 等待通道 */
static int cons_fg_pid;			/* 前台进程：Ctrl+C → SIGINT */

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
	/* 将新控制台插入链表头部 */
	con->next = consoles;
	consoles = con;
}

void console_write(const char *s, unsigned int n)
{
	struct console *con;

	if (!s || n == 0)
		return;

	/* 遍历所有控制台，调用它们的 write 方法 */
	for (con = consoles; con; con = con->next)
		con->write(con, s, n);
}

void consputc(int c)
{
	char ch = (char)c;

	console_write(&ch, 1);
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

static int cons_full(void)
{
	return (cons_w + 1) % CONS_BUF == cons_r;
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
 * Ctrl+C：回显 ^C，向前台发 SIGINT，不入输入缓冲。
 */
void console_intr(int c)
{
	int fg;

	c &= 0xff;
	if (c == ASCII_ETX) {
		acquire(&cons_lock);
		fg = cons_fg_pid;
		release(&cons_lock);
		console_write("^C\n", 3);
		if (fg > 0)
			signal_send(fg, SIGINT);
		return;
	}

	acquire(&cons_lock);
	if (!cons_full()) {
		cons_buf[cons_w] = (char)c;
		cons_w = (cons_w + 1) % CONS_BUF;
	}
	release(&cons_lock);
	/* 唤醒等待控制台输入的进程 */
	wakeup(&cons_chan);
}

/*
 * 读一个输入字符。若被信号打断返回 -1（供 read 返回错误）。
 */
int console_getc(void)
{
	int c;
	struct proc *p;

	if (!cons_cansleep()) {
		/* 启动早期：轮询缓冲（中断尚未开时通常为空） */
		while (cons_empty())
			;
		acquire(&cons_lock);
		c = cons_buf[cons_r] & 0xff;
		cons_r = (cons_r + 1) % CONS_BUF;
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
	c = cons_buf[cons_r] & 0xff;
	cons_r = (cons_r + 1) % CONS_BUF;
	release(&cons_lock);
	return c;
}

void console_init(void)
{
	initlock(&cons_lock, "console");
	cons_r = cons_w = 0;
	cons_fg_pid = 0;

	register_console(&uart_console);
	register_console(&vga_console);
}
