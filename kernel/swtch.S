# Context switch
#
#   void swtch(struct context *old, struct context *new);
#
# Save current registers in old. Load from new.

.globl swtch
swtch:
        # sd 指令 (Store Doubleword), 将源寄存器中的 64位（8字节）数据，
        # 写入到指定的内存地址中
        # sd x10, 16(x2) 表示将 x10 寄存器中的 64位值，写入到 x2 + 16 的内存地址中
        # swtch 函数中，sd ra, 0(a0) 保存的是调用 swtch 函数的下一条指令的地址（即返回地址）
        sd ra, 0(a0)
        sd sp, 8(a0)
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)

        # ld 指令 (Load Doubleword), 从内存中读取一个 64位（8字节）的数据，
        # 并将其加载到目标寄存器中
        ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)

        # ret 指令执行完后，CPU 的特权模式（Mode）不会发生任何变化
        # CPU 在返回时改变特权模式（例如从内核态返回用户态），必须使用专门的特权返回指令：
        # - mret：从 M-mode 返回。硬件会读取 mstatus 寄存器中的 MPP（Machine
        #         Previous Privilege）字段，将 CPU 切换到该字段记录的特权级，并从
        #         mepc 恢复 PC。
        # - sret：从 S-mode 返回。硬件会读取 sstatus 寄存器中的 SPP（Supervisor
        #         Previous Privilege）字段，将 CPU 切换到该字段记录的特权级，并从
        #         sepc 恢复 PC。
        # - uret：从 U-mode 返回（如果硬件支持）。

        # ret 跳转依赖于软件层面的约定
        # 从链接寄存器 ra 中读取之前保存的返回地址, 将该地址加载到程序计数器（pc）中
        ret
