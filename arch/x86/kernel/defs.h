/** interrupt.c */
void idt_set_gate(int vec, void (*handler)(void));
void idt_load(void);

/** printf.c */
void printf(char *fmt, ...);
void panic(char *s);

/** serial.c */
void serial_init(void);
void uart_putc(char c);
void uart_puts(const char *s);

/** timer.c */
void timer_init(void);
unsigned int timer_ticks(void);