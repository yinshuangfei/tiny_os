//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
//

#include "types.h"
#include "spinlock.h"
#include "defs.h"
#include "file.h"
#include "types.h"
#include "proc.h"

#define BACKSPACE 0x100
#define C(x)  ((x)-'@')  // Control-x

struct {
	struct spinlock lock;

	// input
#define INPUT_BUF 128
	char buf[INPUT_BUF];
	uint r;  // Read index
	uint w;  // Write index
	uint e;  // Edit index
} cons;

//
// send one character to the uart.
// called by printf, and to echo input characters,
// but not from write().
//
void consputc(int c)
{
	if (c == BACKSPACE) {
		// if the user typed backspace, overwrite with a space.
		uartputc_sync('\b');
		uartputc_sync(' ');
		uartputc_sync('\b');
	} else {
		uartputc_sync(c);
	}
}

//
// user write()s to the console go here.
//
int consolewrite(int user_src, uint64 src, int n)
{
	int i;

	acquire(&cons.lock);
	for (i = 0; i < n; i++) {
		char c;
		if (either_copyin(&c, user_src, src + i, 1) == -1)
			break;
		uartputc(c);
	}
	release(&cons.lock);

	return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dist indicates whether dst is a user
// or kernel address.
//
int consoleread(int user_dst, uint64 dst, int n)
{
	uint target;
	int c;
	char cbuf;

	target = n;
	acquire(&cons.lock);

	while (n > 0) {
		// wait until interrupt handler has put some
		// input into cons.buffer.
		while (cons.r == cons.w) {
			if (myproc()->killed) {
				release(&cons.lock);
				return -1;
			}
			// sleep(&cons.r, &cons.lock);
		}

		c = cons.buf[cons.r++ % INPUT_BUF];

		if (c == C('D')) {  // end-of-file
			if (n < target) {
				// Save ^D for next time, to make sure
				// caller gets a 0-byte result.
				cons.r--;
			}
			break;
		}

		// copy the input byte to the user-space buffer.
		cbuf = c;
		if (either_copyout(user_dst, dst, &cbuf, 1) == -1)
			break;

		dst++;
		--n;

		if (c == '\n') {
			// a whole line has arrived, return to
			// the user-level read().
			break;
		}
	}
	release(&cons.lock);

	return target - n;
}

void consoleinit(void)
{
	initlock(&cons.lock, "cons");

	uartinit();

	// connect read and write system calls
	// to consoleread and consolewrite.
	devsw[CONSOLE].read = consoleread;
	devsw[CONSOLE].write = consolewrite;
}