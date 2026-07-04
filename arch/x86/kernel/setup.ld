SETUP_BASE = 0x7e00;

SECTIONS
{
	. = SETUP_BASE;

	.text : {
		KEEP(boot/setup.o(.text*))
		setup_end = .;
	}
}
