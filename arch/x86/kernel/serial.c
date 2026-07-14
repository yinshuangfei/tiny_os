#include "types.h"
#include "defs.h"
#include "x86.h"
#include "spinlock.h"
#include "proc.h"
#include "proc_lock.h"
#include "timer.h"

/** 16550 串口寄存器地址 */
#define SERIAL_COM1 0x3f8
#define SERIAL_COM2 0x2f8
#define SERIAL_COM3 0x3e8
#define SERIAL_COM4 0x2e8

#define SERIAL_COM1_RBR SERIAL_COM1       // Receiver Buffer Register
#define SERIAL_COM1_THR SERIAL_COM1       // Transmitter Holding Register
#define SERIAL_COM1_IER (SERIAL_COM1 + 1) // Interrupt Enable Register
#define SERIAL_COM1_IIR (SERIAL_COM1 + 2) // Interrupt Identification Register
#define SERIAL_COM1_LCR (SERIAL_COM1 + 3) // Line Control Register
#define SERIAL_COM1_MCR (SERIAL_COM1 + 4) // Modem Control Register
#define SERIAL_COM1_LSR (SERIAL_COM1 + 5) // Line Status Register
#define SERIAL_COM1_MSR (SERIAL_COM1 + 6) // Modem Status Register
#define SERIAL_COM1_SCR (SERIAL_COM1 + 7) // Scratch Register

/* IER — Interrupt Enable Register（中断使能）*/
#define IER_RDA		0x01	/* 收到数据时中断，接收数据可用 */
#define IER_THRE	0x02	/* 发送保持寄存器空时中断 */

/* LSR — Line Status Register（线路状态）*/
#define LSR_DR		0x01	/* 接收缓冲里有数据可读 */
#define LSR_THRE	0x20	/* 发送保持寄存器空，可以再写下一字节 */

#define UART_BUF	1024

static struct spinlock uart_lock;

/* 接收缓冲区 */
static char rx_buf[UART_BUF];
/* 发送缓冲区 */
static char tx_buf[UART_BUF];

/* 接收缓冲区指针 */
static uint rx_r, rx_w;
/* 发送缓冲区指针 */
static uint tx_r, tx_w;

/* 接收中断唤醒通道 */
static char uart_rx_chan;
/* 发送中断唤醒通道 */
static char uart_tx_chan;

static int rx_empty(void)
{
	return rx_r == rx_w;
}

static int rx_full(void)
{
	return (rx_w + 1) % UART_BUF == rx_r;
}

static int tx_empty(void)
{
	return tx_r == tx_w;
}

static int tx_full(void)
{
	return (tx_w + 1) % UART_BUF == tx_r;
}

/* 持有 uart_lock；中断关闭时可睡眠（printf 持锁路径不可） */
static int uart_cansleep(void)
{
	struct proc *p = myproc();

	return p && p->state == RUNNING && mycpu()->noff == 0;
}

/*
 * 持有 lk 进入；在 proc_lock 下释放 lk 并登记睡眠，唤醒后重新 acquire(lk)。
 * 避免「检查缓冲为空 → sleep」之间丢失 wakeup。
 */
static void sleep_chan(void *chan, struct spinlock *lk)
{
	struct proc *p = myproc();

	if (!holding(lk))
		panic("sleep_chan");
	if (!chan)
		panic("sleep_chan: null chan");

	acquire(&proc_lock);
	release(lk);
	p->chan = chan;
	p->wakeup_tick = 0;
	p->swtched = 1;
	p->state = SLEEPING;
	release(&proc_lock);
	sched();
	acquire(lk);
}

/* 启动发送中断 */
static void uart_start(void)
{
	uint8 ier;

	/* 如果发送缓冲区为空，则关闭发送中断 */
	if (tx_empty()) {
		ier = inb(SERIAL_COM1_IER);
		outb(SERIAL_COM1_IER, ier & ~IER_THRE);
		return;
	}

	/* 如果发送保持寄存器空，则打开发送中断 */
	if ((inb(SERIAL_COM1_LSR) & LSR_THRE) == 0) {
		ier = inb(SERIAL_COM1_IER);
		outb(SERIAL_COM1_IER, ier | IER_THRE);
		return;
	}

	outb(SERIAL_COM1_THR, tx_buf[tx_r]);
	tx_r = (tx_r + 1) % UART_BUF;
	ier = inb(SERIAL_COM1_IER);
	if (tx_empty()) {
		outb(SERIAL_COM1_IER, ier & ~IER_THRE);
	} else {
		outb(SERIAL_COM1_IER, ier | IER_THRE);
	}
}

void serial_init(void)
{
	initlock(&uart_lock, "uart");

	// 写 IER（中断使能），0x00 = 关闭所有串口中断
	outb(SERIAL_COM1_IER, 0x00);
	// 写 LCR bit7=1（DLAB），才能访问波特率除数寄存器
	outb(SERIAL_COM1_LCR, 0x80);
	// 除数低字节 = 3（配合下一行，除数=3 → 115200 波特率 @ 常见时钟）
	outb(SERIAL_COM1, 0x03);
	// 除数高字节 = 0
	outb(SERIAL_COM1_IER, 0x00);
	// LCR = 0x03：DLAB=0，8 数据位、无校验、1 停止位（8N1）
	outb(SERIAL_COM1_LCR, 0x03);
	// FCR：使能 FIFO、清空 RX/TX、14 字节触发阈值
	outb(SERIAL_COM1_IIR, 0xc7);
	// MCR：DTR、RTS、OUT2 置位（OUT2 在 PC 上常接中断线，QEMU 需要）
	outb(SERIAL_COM1_MCR, 0x0b);

	/* 清空可能残留的收发状态 */
	inb(SERIAL_COM1_RBR);
	inb(SERIAL_COM1_IIR);
	inb(SERIAL_COM1_LSR);
	inb(SERIAL_COM1_MSR);

	/* 仅开接收中断；发送中断在有数据时由 uart_start 打开 */
	outb(SERIAL_COM1_IER, IER_RDA);
}

/* IRQ4：串口中断处理 */
void uart_intr(void)
{
	int got_rx = 0;
	int put_tx = 0;

	/* 发送 EOI（中断结束）， 允许下一个中断 */
	pic_eoi(4);

	acquire(&uart_lock);

	while (inb(SERIAL_COM1_LSR) & LSR_DR) {
		char c = inb(SERIAL_COM1_RBR);

		/* 如果接收缓冲区未满，则将数据写入接收缓冲区 */
		if (!rx_full()) {
			rx_buf[rx_w] = c;
			rx_w = (rx_w + 1) % UART_BUF;
			got_rx = 1;
		}
	}

	/* 如果发送保持寄存器空，并且发送缓冲区不为空，则发送数据 */
	while ((inb(SERIAL_COM1_LSR) & LSR_THRE) && !tx_empty()) {
		outb(SERIAL_COM1_THR, tx_buf[tx_r]);
		tx_r = (tx_r + 1) % UART_BUF;
		put_tx = 1;
	}

	/* 如果发送缓冲区为空，则关闭发送中断 */
	if (tx_empty()) {
		uint8 ier = inb(SERIAL_COM1_IER);
		outb(SERIAL_COM1_IER, ier & ~IER_THRE);
	}

	release(&uart_lock);

	if (got_rx)
		wakeup(&uart_rx_chan);
	if (put_tx)
		wakeup(&uart_tx_chan);
}

/* 忙等写一字节（LSR bit5 = THR 空） */
static void uart_putc_busy(char c)
{
	while ((inb(SERIAL_COM1_LSR) & LSR_THRE) == 0)
		;
	outb(SERIAL_COM1_THR, c);
}

/* 忙等读一字节（LSR bit0 = DR 有数据） */
static int uart_getc_busy(void)
{
	while ((inb(SERIAL_COM1_LSR) & LSR_DR) == 0)
		;
	return inb(SERIAL_COM1_RBR) & 0xff;
}

void uart_putc(char c)
{
	/*
	 * 持有 spinlock / 中断上下文 / 尚无进程时不能 sleep：
	 * 回退为 LSR 忙等直写（printf 路径走这里）。
	 */
	if (!uart_cansleep()) {
		uart_putc_busy(c);
		return;
	}

	acquire(&uart_lock);
	while (tx_full())
		sleep_chan(&uart_tx_chan, &uart_lock);
	tx_buf[tx_w] = c;
	tx_w = (tx_w + 1) % UART_BUF;
	uart_start();
	release(&uart_lock);
}

void uart_puts(const char *s)
{
	while (*s)
		uart_putc(*s++);
}

int uart_getc(void)
{
	int c;

	if (!uart_cansleep())
		return uart_getc_busy();

	acquire(&uart_lock);
	while (rx_empty())
		sleep_chan(&uart_rx_chan, &uart_lock);
	c = rx_buf[rx_r] & 0xff;
	rx_r = (rx_r + 1) % UART_BUF;
	release(&uart_lock);
	return c;
}
