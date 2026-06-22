#include "memlayout.h"

//
// low-level driver routines for 16550a UART.
//

// the UART control registers are memory-mapped
// at address UART0. this macro returns the
// address of one of the registers.
#define Reg(reg) ((volatile unsigned char *)(UART0 + reg))

// the UART control registers.
// some have different meanings for
// read vs write.
// see http://byterunner.com/16550.html
#define RHR 0                 // receive holding register (for input bytes)
#define THR 0                 // transmit holding register (for output bytes)
#define IER 1                 // interrupt enable register
#define IER_TX_ENABLE (1<<0)
#define IER_RX_ENABLE (1<<1)
#define FCR 2                 // FIFO control register
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR (3<<1) // clear the content of the two FIFOs
#define ISR 2                 // interrupt status register
#define LCR 3                 // line control register
#define LCR_EIGHT_BITS (3<<0)
#define LCR_BAUD_LATCH (1<<7) // special mode to set baud rate
#define LSR 5                 // line status register
#define LSR_RX_READY (1<<0)   // input is waiting to be read from RHR
#define LSR_TX_IDLE (1<<5)    // THR can accept another character to send

#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))


// add a character to the output buffer and tell the
// UART to start sending if it isn't already.
// blocks if the output buffer is full.
// because it may block, it can't be called
// from interrupts; it's only suitable for use
// by write().
void
uartputc(int c)
{
//   acquire(&uart_tx_lock);

//   if(panicked){
//     for(;;)
//       ;
//   }

//   while(1){
//     if(((uart_tx_w + 1) % UART_TX_BUF_SIZE) == uart_tx_r){
//       // buffer is full.
//       // wait for uartstart() to open up space in the buffer.
//       sleep(&uart_tx_r, &uart_tx_lock);
//     } else {
//       uart_tx_buf[uart_tx_w] = c;
//       uart_tx_w = (uart_tx_w + 1) % UART_TX_BUF_SIZE;
//       uartstart();
//       release(&uart_tx_lock);
//       return;
//     }
//   }
}

// alternate version of uartputc() that doesn't
// use interrupts, for use by kernel printf() and
// to echo characters. it spins waiting for the uart's
// output register to be empty.
void uartputc_sync(int c)
{
//   push_off();

//   if(panicked){
//     for(;;)
//       ;
//   }

	// wait for Transmit Holding Empty to be set in LSR.
	// while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
	// 	;
	WriteReg(THR, c);

//   pop_off();
}