        #
        # code to switch between user and kernel space.
        #
        # this code is mapped at the same virtual address
        # (TRAMPOLINE) in user and kernel space so that
        # it continues to work when it switches page tables.
        #
        # kernel.ld causes this to be aligned
        # to a page boundary.
        #
.section trampsec
.globl trampoline
# trampoline 是这页代码的起始符号
trampoline:
# 对齐到 16 字节边界
.align 4
.globl uservec
# 重点！！！uservec 是"用户态 trap 进内核时"的入口点，模式为 supervisor (内核态),
# 使用用户页表, 因此, 进入 kernel_trap 指向的 usertrap() 函数时，已经为内核模式.
# 总结：从用户态进入内核态，uservec -> usertrap()
# 触发源见 usertrapret() 函数

# 用户态进入内核态(此时已经是内核态), uservec -> usertrap()
#  - 用户态程序触发 trap 的那一刻，CPU 硬件会自动把特权级从 U-mode 切到 S-mode
#  - 然后才跳到 stvec 指向的入口，也就是 uservec
#  - 所以 uservec 运行时，已经是在 supervisor mode，也就是内核态

# 当 CPU 从用户态（U-mode）发生陷阱（如系统调用、异常或中断）并跳转到 uservec 时，
# RISC-V 硬件会自动执行以下操作：
#  - 自动关闭全局中断: 硬件会自动清除 sstatus 寄存器中的 SIE
#  - 保存中断状态: 硬件会将陷阱发生前 SIE 的原始状态保存在 sstatus 寄存器的 SPIE
#  - 保存现场信息: 同时，硬件还会将当前的程序计数器（PC）保存到 sepc
#  - sp 保持不变
uservec:
        #
        # trap.c sets stvec to point here, so
        # traps from user space start here,
        # in supervisor mode, but with a
        # user page table.
        #
        # sscratch points to where the process's p->trapframe is
        # mapped into user space, at TRAPFRAME.
        #
        # sscratch 作为临时寄存器，通常用来存放 Trapframe 的内存基地址

        # swap a0 and sscratch
        # so that a0 is TRAPFRAME
        csrrw a0, sscratch, a0

        # save the user registers in TRAPFRAME
        # 将 ra 中的 64 位数据，存储到 a0 + 40 的内存地址中
        # 将用户态运行的'热乎状态'保存起来
        sd ra, 40(a0)
        sd sp, 48(a0)       # 保存用户栈地址
        sd gp, 56(a0)
        sd tp, 64(a0)
        sd t0, 72(a0)
        sd t1, 80(a0)
        sd t2, 88(a0)
        sd s0, 96(a0)
        sd s1, 104(a0)
        sd a1, 120(a0)
        sd a2, 128(a0)
        sd a3, 136(a0)
        sd a4, 144(a0)
        sd a5, 152(a0)
        sd a6, 160(a0)
        sd a7, 168(a0)
        sd s2, 176(a0)
        sd s3, 184(a0)
        sd s4, 192(a0)
        sd s5, 200(a0)
        sd s6, 208(a0)
        sd s7, 216(a0)
        sd s8, 224(a0)
        sd s9, 232(a0)
        sd s10, 240(a0)
        sd s11, 248(a0)
        sd t3, 256(a0)
        sd t4, 264(a0)
        sd t5, 272(a0)
        sd t6, 280(a0)

        # save the user a0 in p->trapframe->a0
        csrr t0, sscratch
        sd t0, 112(a0)

        # restore kernel stack pointer from p->trapframe->kernel_sp
        # 从 a0 + 8 的内存地址中，读取一个 64 位的数据加载到栈指针寄存器 sp 中
        # 用户栈切换到内核栈
        ld sp, 8(a0)

        # make tp hold the current hartid, from p->trapframe->kernel_hartid
        ld tp, 32(a0)

        # load the address of usertrap(), p->trapframe->kernel_trap
        ld t0, 16(a0)

        # restore kernel page table from p->trapframe->kernel_satp
        ld t1, 0(a0)
        csrw satp, t1
        sfence.vma zero, zero

        # a0 is no longer valid, since the kernel page
        # table does not specially map p->tf.

        # jump to usertrap(), which does not return
        jr t0

# 从内核态返回到用户态
.globl userret
userret:
        # userret(TRAPFRAME, pagetable)
        # switch from kernel to user.
        # usertrapret() calls here. usertrapret() 函数里调用这里.
        # a0: TRAPFRAME, in user page table.
        # a1: user page table, for satp.

        # userret 的语义也不是“正常返回”：
        #   - 它先切到用户页表
        #   - 恢复用户寄存器
        #   - 最后执行 sret 回到用户态

        # switch to the user page table.
        csrw satp, a1
        sfence.vma zero, zero

        # put the saved user a0 in sscratch, so we
        # can swap it with our a0 (TRAPFRAME) in the last step.
        ld t0, 112(a0)
        # 将通用寄存器 t0 的值，写入到 sscratch 控制与状态寄存器中
        csrw sscratch, t0

        # restore all but a0 from TRAPFRAME
        # 将 p->trapframe 中的值加载到对应的寄存器中，恢复用户进程
        ld ra, 40(a0)
        ld sp, 48(a0)       # 还原用户栈地址
        ld gp, 56(a0)
        ld tp, 64(a0)
        ld t0, 72(a0)
        ld t1, 80(a0)
        ld t2, 88(a0)
        ld s0, 96(a0)
        ld s1, 104(a0)
        ld a1, 120(a0)
        ld a2, 128(a0)
        ld a3, 136(a0)
        ld a4, 144(a0)
        ld a5, 152(a0)
        ld a6, 160(a0)
        ld a7, 168(a0)
        ld s2, 176(a0)
        ld s3, 184(a0)
        ld s4, 192(a0)
        ld s5, 200(a0)
        ld s6, 208(a0)
        ld s7, 216(a0)
        ld s8, 224(a0)
        ld s9, 232(a0)
        ld s10, 240(a0)
        ld s11, 248(a0)
        ld t3, 256(a0)
        ld t4, 264(a0)
        ld t5, 272(a0)
        ld t6, 280(a0)

        # restore user a0, and save TRAPFRAME in sscratch
        # 原子交换（Swap）, 交换 a0 和 sscratch
        csrrw a0, sscratch, a0

        # return to user mode and user pc.
        # usertrapret() set up sstatus and sepc.
        # 返回到 epc 指向的用户态程序地址执行

        # 跳转目标是由硬件自动管理
        # CPU 发生异常或执行 ecall 陷入内核时，硬件会自动把当前的 PC（程序计数器）保存
        # 到 sepc 寄存器中。执行 sret 时，硬件直接将 sepc 的值赋给 PC
        sret
