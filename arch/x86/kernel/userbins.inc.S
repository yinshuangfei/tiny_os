/*
 * 将 user/*.bin 嵌入内核 .rodata，供 execve / uthread_create 加载。
 */
.section .rodata
.align 4
.global loader
.global loader_end
loader:
.incbin "user/loader.bin"
loader_end:

.global initcode
.global initcode_end
initcode:
.incbin "user/initcode.bin"
initcode_end:
