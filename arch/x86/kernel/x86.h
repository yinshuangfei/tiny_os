/** set interrupt flag */
static inline void sti(void)
{
	__asm__ volatile ("sti");
}

/** clear interrupt flag */
static inline void cli(void)
{
	__asm__ volatile ("cli");
}

/** halt */
static inline void halt(void)
{
	__asm__ volatile ("hlt");
}

/** outb */
static inline void outb(unsigned short port, unsigned char val)
{
	__asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}