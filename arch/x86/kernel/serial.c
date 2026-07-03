#include "defs.h"
#include "x86.h"

/** 串口寄存器地址 */
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

extern void serial_putc(char c);

void serial_init(void)
{
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
}

void uart_putc(char c)
{
	serial_putc(c);
}

void uart_puts(const char *s)
{
	while (*s)
		uart_putc(*s++);
}