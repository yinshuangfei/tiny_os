/*
 * 将 user/initcode.bin 嵌入内核 .rodata，供 uvminit 拷贝到用户页。
 */
.section .rodata
.align 4
.global initcode
.global initcode_end
initcode:
.incbin "user/initcode.bin"
initcode_end:
