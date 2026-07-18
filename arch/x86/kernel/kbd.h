#ifndef __KBD_H__
#define __KBD_H__

/* PS/2 键盘（8042 + IRQ1） */
void kbd_init(void);
void kbd_intr(void);

#endif
