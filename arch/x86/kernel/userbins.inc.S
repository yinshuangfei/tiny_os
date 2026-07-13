/*
 * 将 user/*.bin 嵌入内核 .rodata，供 execve / kernel_execve 加载。
 */
.section .rodata
.align 4

.global init
.global init_end
init:
.incbin "user/init.bin"
init_end:

.global sh
.global sh_end
sh:
.incbin "user/sh.bin"
sh_end:
