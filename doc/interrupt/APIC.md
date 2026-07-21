APIC 的全称是 Advanced Programmable Interrupt Controller（高级可编程中断控制器）。它是现代多核/多处理器计算机中，用于管理和分配硬件中断的核心组件。

## 诞生背景
在早期的单核计算机中，使用的是 8259A 芯片来处理中断。但随着多处理器（SMP）系统的出现，8259A 无法高效地将中断分配给不同的 CPU，因此 Intel 从 Pentium 处理器开始引入了 APIC 架构。

## 核心组成
现代 APIC 系统主要由两部分组成：
- Local APIC (本地 APIC)：集成在每个 CPU 核心内部。负责接收中断请求、管理本地中断优先级，以及生成处理器间中断（IPI），实现多核之间的通信与任务调度。
- I/O APIC (输入输出 APIC)：通常位于主板的芯片组（如南桥）中。负责收集来自外部硬件设备（如网卡、键盘、鼠标等）的中断信号，并根据路由表将其转发给合适的 CPU 核心处理。
### LAPIC 和 IOAPIC 地址
- LAPIC: 0xFEE00000, 地址空间大小 4 KB。
在多处理器系统中，每个 CPU 核心都有自己独立的 LAPIC。虽然它们初始都映射到 0xFEE00000 这个相同的物理地址，但硬件会自动将其路由到当前正在执行访问指令的 CPU 的本地 LAPIC。
- IOAPIC: 0xFEC00000, 地址空间大小 4 KB。
与 LAPIC 不同，IOAPIC 通常位于主板芯片组中，一个系统可以拥有多个 IOAPIC。因此，IOAPIC 的基地址不是固定不变的。在实际的操作系统开发中，不能硬编码这个地址，而必须在系统启动时通过解析 ACPI 表（具体为 MADT，即 Multiple APIC Description Table）来动态获取每个 IOAPIC 的确切物理基地址。

## 核心作用
极大地扩展了系统可用的中断数量（可达上百个），并允许操作系统灵活地将外部设备的请求分配给空闲或最合适的 CPU 核心，从而大幅提升多核系统的并发处理效率。


# 寄存器
## RDMSR
RDMSR 是 x86 架构中的一条核心特权指令，全称为 Read Model-Specific Register（读取模型特定寄存器）。
RDMSR 的作用就是读取 MSR（模型特定寄存器）的值。MSR 是一组用于控制 CPU 运行状态、性能监控、调试以及电源管理等底层行为的特殊寄存器。

当 CPU 执行 RDMSR 指令时，它需要知道你想读取哪一个具体的 MSR。其工作机制如下：
- 输入参数：程序员必须将要读取的 MSR 的编号（索引）放入 ECX 寄存器中。
- 输出结果：由于 MSR 通常是 64 位宽的，CPU 会将读取到的 64 位数据拆分为两部分：
    低 32 位存入 EAX 寄存器。
    高 32 位存入 EDX 寄存器。

与 RDMSR 对应的是 WRMSR（Write Model-Specific Register）指令，它的作用是将 EAX 和 EDX 中的 64 位数据写入到 ECX 指定的 MSR 中。

操作系统和底层开发者通常在以下场景使用 RDMSR：
- 性能监控：读取 CPU 内部的性能计数器（Performance Counters），例如统计缓存未命中次数、分支预测失败次数、执行的指令总数等。
- 电源管理：读取或配置 CPU 的频率、电压状态（如 P-states、C-states），以实现动态频率调节和节能。
- 虚拟化：Hypervisor（如 KVM、VMware）使用它来配置和管理虚拟机的底层硬件行为（例如 Intel VT-x 相关的控制寄存器）。
- 硬件特性检测：检测 CPU 是否支持某些特定的扩展功能或微架构特性。

MSR（Model Specific Register，模型特定寄存器）的数量非常庞大，随着 CPU 的更新换代，目前其数量已远超 200 个7。它们的编号（即 MSR Index）通常以十六进制表示。
虽然不同 CPU 厂商（如 Intel 和 AMD）甚至不同代际的 CPU 都有自己独有的 MSR，但为了保证跨代际和跨型号的兼容性，Intel 固化了一批“架构 MSR”（Architectural MSR），通常以 IA32_ 作为前缀。
- 系统调用相关 MSR
- 长模式与系统特性控制 MSR
    - 0xC0000080 (IA32_EFER)：扩展功能启用寄存器。用于控制 IA-32e 模式（长模式）的激活、系统调用扩展开关（SCE）以及 NX（不可执行）位的开启等。
- 早期快速系统调用 MSR
- 性能监控与硬件配置 MSR
    - 0x00000010 (IA32_TIME_STAMP_COUNTER)：时间戳计数器（TSC），每个 CPU 时钟周期递增一次，常用于高精度的性能测试和时间测量。
    - 0x0000001B (IA32_APIC_BASE)：本地 APIC（高级可编程中断控制器）的基地址寄存器，用于配置 APIC 的内存映射地址及启用/禁用状态。
    - 0xC0000102 (IA32_KERNEL_GS_BASE)：配合 SWAPGS 指令使用，常用于操作系统在内核态访问 per-CPU 变量。


# PIT/HPET 是什么？
1. PIT (Programmable Interval Timer，可编程间隔定时器)
背景：这是早期 PC 架构（如 8086 时代）中使用的传统计时器电路。
特点：PIT 通常以非常小的时间间隔用于周期模式。虽然它也可以进入单触发模式，但设置过程非常慢，在实际应用中无法用于需要精确调度的任务。
地位：随着技术的发展，PIT 逐渐被更先进的计时器所补充和替换。
2. HPET (High Precision Event Timer，高精度事件计时器)
背景：HPET 是由英特尔（Intel）与微软共同开发的一种硬件计时器，自 2005 年以来已被纳入 PC 芯片组（通常集成在南桥芯片中）。
特点：与老式的 PIT 相比，HPET 具有更高的频率（至少 10 MHz）和更宽的 64 位计数器，能够提供比 RTC（实时时钟）更高的分辨率来产生周期性中断。它的单触发计时器设置成本极小，避免了额外的中断开销。
应用：HPET 经常用于同步多媒体流、提供平滑播放，以及为每个 CPU 的调度提供计时器。在现代操作系统（如 Windows 10）中，它被用来改善硬件中的事件计时，特别是在视频游戏、音视频编辑等需要高精度的场景中。


# linux 中的中断设计
```
  设备 IRQ（键盘/磁盘…）     时钟（tick / oneshot）
           │                         │
           ▼                         ▼
       I/O APIC ────────────►  Local APIC ──► 每 CPU 收中断
           │                         │
           │                    LVT Timer
           │                    （周期或单次）
           ▼                         ▼
        设备 handler            clockevent → timer_interrupt
                                         → update_process_times
                                         → nanosleep 唤醒 …
```
|角色	|Linux|
|:---|:---|
|设备中断路由|IOAPIC / MSI|
|时钟事件|优先 LAPIC timer（lapic_clockevent）|
|PIT (8254)|早期 boot、校准、无 APIC 时兜底|
|8259 PIC|早期 / 无 APIC；有 IOAPIC 后基本不用|

APIC 模式 = LAPIC timer + IOAPIC 设备,
Local APIC timer 负责 tick，IOAPIC 负责外设。