#ifndef __VGA_H__
#define __VGA_H__

/* VGA 文本模式（80x25，显存 0xb8000） */
void vga_init(void);
void vga_putc(char c);
void vga_putc_at(int row, int col, char c);

#endif
