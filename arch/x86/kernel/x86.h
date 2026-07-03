static inline void sti(void)
{
	__asm__ volatile ("sti");
}

static inline void outb(unsigned short port, unsigned char val)
{
	__asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}