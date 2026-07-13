# trampoline 和 uservec 是同一个地址吗?
代码当前布局下：uservec 和 trampoline 符号地址相同.


# 镜像加载到内存中的布局
0x7c00 ─┬─ boot sector（MBR，BIOS 放入，512 字节）
        │   … 代码、boot_drive …
0x7DF0  │   boot_params（扇区数等）
0x7DFF ─┘
0x7e00 ─┬─ kernel.bin 开始（setup + body）← 你们 STAGE2_TMP
        │
0x10000 ┼─ 第二段 DMA 接着写（若超过 65 扇区）
        …

0x7c00 + 512 = 0x7e00