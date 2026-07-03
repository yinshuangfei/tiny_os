.code32

.globl timervec
.type timervec, @function

timervec:
	pusha
	call timer_handler
	popa
	movb $0x20, %al
	outb %al, $0x20
	iret
