        # qemu -kernel loads the kernel at 0x80000000
        # and causes each CPU to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
.section .text
.globl _entry

_entry:
        # set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        # sp = stack0 + (hartid * 4096)
        # smp 中，每个核心都会执行下列代码

        # 加载地址
        la sp, stack0
        # 加载立即数
        li a0, 1024*4
        # CSR Read（读取控制与状态寄存器, 读取当前 CPU 核心的硬件线程 ID）
        csrr a1, mhartid
        # Add Immediate（加立即数）
        addi a1, a1, 1
        # 将 a0 和 a1 相乘，结果存回 a0
        mul a0, a0, a1
        # sp = stack0 的基地址 + 4096 × (mhartid + 1)
        add sp, sp, a0

        # jump to start() in start.c
        call start
spin:
        j spin
