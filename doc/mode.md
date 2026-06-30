### M-mode
进入 M-mode 时，RISC-V 硬件会自动执行以下操作：
  - 保存当前执行位置到 mepc
  - 把当前特权级记录到 mstatus.MPP
  - 把 mstatus.MPIE 设成原来的中断使能状态
  - 清掉 mstatus.MIE
  - 把 mcause 设成 machine timer interrupt
  - 跳到 mtvec 指向的地址，也就是 timervec
mret 时，硬件做什么:
  - pc <- mepc
  - mstatus.MIE <- mstatus.MPIE
  - mstatus.MPIE <- 1
  - mstatus.MPP 复位到之前的特权级

### trap
trap 类型由最高位决定，
- Interrupt = 1, 异步中断
    1:  Supervisor software interrupt
    5:  Supervisor timer interrupt
    9:  Supervisor external interrupt
    13: Counter-overflow interrupt
- Interrupt = 0, 同步异常(异常|系统调用)
    0: Instruction address misaligned
    1: Instruction access fault
    2: Illegal instruction
    3: Breakpoint
    4: Load address misaligned
    5: Load access fault
    6: Store/AMO address misaligned
    7: Store/AMO access fault
    8: Environment call from U-mode
    9: Environment call from S-mode
    12: Instruction page fault
    13: Load page fault
    15: Store/AMO page fault
    18: Software check
    19: Hardware error
    24 ... 31: "Designated for custom use
    48 ... 63: Designated for custom use

### M-mode, S-mode, U-mode 中的中断各自的控制范围是怎么样的，使能和不使能的作用是什么?
  1. M-mode
  M-mode 是最高特权级，控制的是“机器级中断”。
  M-mode 的控制范围是：
  - mtvec：M-mode 中断入口
  - mie：各类机器级中断源使能
  - mstatus.MIE：M-mode 总开关
  - mideleg/medeleg：把某些中断/异常下放给 S-mode

  使能 的作用是：允许 CPU 真的去取这个级别、这个来源的中断。
  不使能 的作用是：中断即使来了，也只是挂起，不会立刻进入对应 trap。

  2. S-mode
  S-mode 是操作系统工作的主要特权级，它控制的是“监督者级中断”。
  S-mode 的控制范围是：
  - stvec：S-mode trap 入口
  - sie：S-mode 各中断源使能
  - sstatus.SIE：S-mode 总开关
  - sip：挂起状态位
  - 以及 PLIC 里给 S-mode 的外部中断授权

  intr_on() / intr_off() 实际上只是开关 sstatus.SIE，不是打开所有中断源。也就是说：
  - SIE=1 只是允许 S-mode 接收已经被授权的中断
  - 如果 sie 里某个来源没开，那个来源还是进不来
  - 如果 SIE=0，即使来源开了，当前 S-mode 也不会立刻响应

  3. U-mode
  U-mode 基本不直接控制中断。它只能：
  - 触发同步异常，比如 ecall
  - 被内核允许的异步中断打断，然后通过内核设置好的 trap 路径转走

  在 xv6 这类设计里，U-mode 的“中断能力”本质上是由 S-mode 决定的，不是用户程序自己决定的。用户程序最多是通过 ecall 主动进入内核，或者在返回用户态后，由内核之前设置好的 stvec=uservec、sstatus、sepc 这些状态决定下次怎么被打断、怎么回来，

  4. 使能 / 不使能的实际意义
  对异步中断来说，使能 和 不使能 的核心区别是“会不会立刻进 trap”。

  - 使能：中断源和全局开关都打开时，CPU 会在合适的特权级接管中断
  - 不使能：中断可能先挂起，等你重新打开后再处理
  - 这不影响同步异常，比如 ecall、page fault，这些不靠中断使能位决定

  所以可以概括成：

  - M-mode：管“机器级”中断，最底层
  - S-mode：管“操作系统真正处理的中断”
  - U-mode：自己不直接管中断，只能被内核安排好的机制打断或陷入

### M-mode 中断使能关闭，S-mode 中断使能打开，S-mode 的中断还能接受到吗?
mstatus.MIE 只管 M-mode 的全局中断开关。
sstatus.SIE + sie 才管 S-mode 的中断开关。
- 如果 CPU 当前正在 S-mode，那么 MIE=0 并不会阻止 S-mode 中断
- 如果 CPU 当前正在 M-mode，那 S-mode 中断也不会“直接打进来”，因为现在根本不在 S-mode

  1. 当前在 S-mode
  - MIE=0
  - SIE=1
  - 对应源在 sie 里打开了
  - PLIC / 其他源也授权了
  结果：
  - S-mode 中断可以正常进入

  2. 当前在 S-mode
  - MIE=0
  - SIE=0
  结果：
  - S-mode 中断不进
  - 即使源已经 pending，也会先挂起，等你把 SIE 打开再处理

  3. 当前在 M-mode
  - MIE=0
  结果：
  - M-mode 中断不进
  - S-mode 中断也不会在 M-mode 里被处理，因为当前不在 S-mode

  4. 当前在 M-mode
  - MIE=1
  结果：
  - M-mode 允许接收机器级中断
  - S-mode 的中断仍然要等 CPU 回到 S-mode 后，才按 stvec/sie/sstatus.SIE 规则处理

### S-mode 的中断，无法打断 M-mode 中正在执行的流程吗?
绝对不能。
1. 特权级的绝对压制
RISC-V 的特权级是严格分层的（M-mode > S-mode > U-mode）。
- 当 CPU 处于 M-mode 时，它只响应 M-mode 级别的中断和异常。
- S-mode 的中断（如 S-mode 定时器中断、S-mode 外部中断）属于低特权级事件，硬件在底层设计上就直接屏蔽了它们对 M-mode 的干扰。
2. 中断委托机制（Delegation）的单向性
RISC-V 提供了一个 mideleg（Machine Interrupt Delegation）寄存器，用于决定哪些中断可以“下放”给 S-mode 处理。
- 这个机制是单向的：M-mode 可以选择把某些中断委托给 S-mode；
- 如果某个中断被委托给了 S-mode，那么当 CPU 处于 S-mode 或 U-mode 时，硬件才会去响应它；一旦 CPU 处于 M-mode，硬件会直接忽略这些被委托的 S-mode 中断。

### M-mode 和 S-mode 都设置了中断使能，都开启了某一个中断，都设置了中断向量，这个中断发生时，什么模式优先响应?
  - 如果这个中断是“可委托”的，而且 mideleg 把它委托给了 S-mode，那么 S-mode 响应，M-mode 不会先来抢。
  - 如果这个中断没有委托给 S-mode，那么 M-mode 优先响应。