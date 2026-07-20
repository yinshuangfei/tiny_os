# FPU
FPU (Floating-Point Unit，浮点处理单元)。
FPU 是计算机中央处理器（CPU）内部的一个专用硬件模块，专门设计用来执行浮点数（带有小数点的数字）的数学运算。
- 核心功能：专门处理诸如加法、减法、乘法、除法、平方根等复杂的数学运算。
- 重要性：在 FPU 出现之前，计算机只能用整数运算来模拟浮点运算，效率非常低。有了 FPU，科学计算、3D图形渲染、音视频处理等需要大量高精度计算的任务才能得到高效执行。
- 历史演变：早期的 FPU 是独立的芯片（也叫数学协处理器），需要额外购买并安装在主板上。从 Intel 80486 时代开始，FPU 被直接集成到了 CPU 内部，成为现代处理器的标准配置。


# SSE
SSE (Streaming SIMD Extensions，单指令多数据流扩展)。
SSE 是 Intel 公司推出的一套指令集（可以理解为给 CPU 下达命令的“语言”或“规则”的扩展），它是对 FPU 能力的一次重大升级。
核心功能：SSE 引入了 SIMD（Single Instruction, Multiple Data）技术。传统的 FPU 一次只能处理一个或两个数据，而 SSE 允许 CPU 用一条指令同时处理多个数据4。
性能提升：这种并行处理方式极大地提高了浮点运算的速度，对 3D 图形、图像处理、视频编解码、音频合成等多媒体应用的性能有显著增强4。
硬件支持：为了支持 SSE 指令集，CPU 内部增加了新的寄存器（如 128 位的 XMM 寄存器）和新的执行单元4

FPU 是负责干活的“工人”，而 SSE 是一套能让工人“一个人同时干几个人的活”的“高效工作方法”。


# FXSAVE 指令
执行 FXSAVE 指令时，它会将以下寄存器和状态信息一次性写入内存：
- x87 FPU 状态：包括传统的浮点寄存器（ST0-MM7）、浮点控制字（FCW）、浮点状态字（FSW）等。
- MMX 技术状态：MMX 寄存器。
- SSE 状态：包括 128 位的 XMM 寄存器（XMM0-XMM7，在 64 位模式下为 XMM0-XMM15）以及 MXCSR 寄存器（控制 SSE 的异常掩码和舍入模式等）。

操作系统任务切换：当操作系统在多个进程或线程之间切换时，必须保存当前任务的浮点/SIMD 状态，并在切换回来时恢复，以防止数据被其他任务污染。


# FXRSTOR 指令
FXRSTOR（Floating-point and SIMD state Restore）是 x86 架构下与 FXSAVE 配套使用的指令，专门用于从内存中恢复处理器的浮点运算和 SIMD（单指令多数据）状态。


# FNINIT
FNINIT（Initialize Floating-Point Unit Without Checking）是 x86 架构下的一条浮点控制指令，专门用于在不检查待处理的未屏蔽浮点异常的情况下，将 FPU（浮点处理单元）初始化为默认的“干净”状态。


# LDMXCSR
LDMXCSR（Load Streaming SIMD Extensions Control/Status Register）是 x86 架构下的一条指令，专门用于从内存中加载数据到 MXCSR 寄存器。


# CR0.TS 的作用
置 CR0.TS（Task Switched）是为了做 lazy FPU 切换：不是每次调度都保存/恢复浮点寄存器，而是“下次真用到浮点时再切”。
- TS 在硬件上干什么
CR0.TS = 1 时，CPU 执行 x87 / SSE 指令会立刻触发 #NM（Device Not Available），而不是直接用当前硬件里的浮点状态。
调度切走进程时置 TS，等于告诉 CPU：
“硬件上的 FPU/SSE 状态可能已经不属于当前进程了，先别用。”

- 和 lazy 切换怎么配合
    - 进程 A → 进程 B：context_switch 里置 TS=1（通用寄存器照常 swtch，浮点先不动）
    - B 若不用浮点：从不触发 #NM，省掉一次 fxsave/fxrstor
    - B 要用浮点：#NM → 清 TS → 若 owner 不是 B，则 fxsave(A)、fxrstor(B)（或初始化）→ 返回重试这条指令
也就是说：TS 是“延迟切换”的钩子；真正切换发生在 #NM 里。

- 为什么不每次切换都 fxsave
浮点状态有 512 字节（FXSAVE），比 ebx/esi/... 贵得多。很多进程/内核路径根本不算浮点，eager 保存浪费大。Lazy + TS 是 Linux 等内核的经典做法。


# eager 保存
Eager（急切）保存，相对的是 Lazy（懒惰）保存。
Eager 保存就是：每次进程切换时，立刻把当前进程的 FPU/SSE 状态存起来，并把下一个进程的状态装回去——不等到对方真的用浮点。

| 维度 | Eager (急切保存) | Lazy (懒惰切换) |
|------|------------------|------------------------|
| 切换时机 | 每次 context_switch / switch | 下次执行 x87/SSE 指令时 |
| 核心操作 | 立刻 fxsave 旧进程 + fxrstor 新进程 | 切走时只 CR0.TS=1, #NM 里再切换 |
| 是否依赖 TS | 一般不需要 | 需要 (TS=1 触发 #NM) |
| 不用浮点的进程 | 仍会 fxsave / fxrstor | 几乎零浮点开销 |
| 单次切换成本 | 固定偏高 (~512B 状态) | 通常更低；首次用浮点时才付账 |
| 实现复杂度 | 更简单、直观 | 稍复杂 (owner + #NM handler) |
| 正确性 | 切换后立刻正确 | 首次 FP 指令前由 #NM 保证正确 |
| 典型场景 | 教学演示、进程几乎都会算浮点 | 多进程、大量进程不用 FP (类 Linux) |