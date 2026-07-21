# cpu 核数静态上界
```
grep CONFIG_NR_CPUS /boot/config-$(uname -r)
CONFIG_NR_CPUS_RANGE_BEGIN=8192
CONFIG_NR_CPUS_RANGE_END=8192
CONFIG_NR_CPUS_DEFAULT=8192
CONFIG_NR_CPUS=8192
```

linux 下，若 NR_CPUS= 4096，实际物理机核心为8192，则只能使用 4096 个核心。
- NR_CPUS 静态上界是编译进内核的硬顶，运行时不能再扩；
- 要吃满 8192，需要重新编译：CONFIG_NR_CPUS ≥ 8192（x86_64 上通常还要 CPUMASK_OFFSTACK / MAXSMP 一类选项）；
- 即使 NR_CPUS 够大，还可能被 maxcpus= 再截断，那时 online 会更少；

## NR_CPUS
NR_CPUS（即 CONFIG_NR_CPUS）是一个在编译期确定的硬限制，它定义了该内核支持的最大逻辑处理器数量。
- 内存预分配：内核在启动时，会根据这个值来预分配内存。例如，每个 CPU 的 per-cpu 数据区、调度队列、以及 cpumask（CPU 掩码位图）等数据结构，都是按照 NR_CPUS 的大小来分配空间的。
- 位图尺寸：内核使用位图来管理 CPU 状态，其尺寸由 BITS_TO_LONGS(NR_CPUS) 计算得出，这意味着超出该位图范围的 CPU 根本无法在内核数据结构中找到对应的位置。
当物理 CPU 数量超过 NR_CPUS 时，多出来的 CPU 会被标记为离线（offline）状态。你可以通过查看 sysfs 中的 CPU 拓扑信息来验证这一点。在 /sys/devices/system/cpu/ 目录下查看。
- /sys/devices/system/cpu/kernel_max：内核配置允许的最大 CPU 下标值（即 NR_CPUS - 1）；
- /sys/devices/system/cpu/present（实际存在）：代表了系统中被识别为实际存在的 CPU 列表（即 cpu_present_mask）；
- /sys/devices/system/cpu/offline（离线）：由于热插拔被移除，或者超过了内核配置允许的最大 CPU 上限（kernel_max）而未上线的 CPU。
- /sys/devices/system/cpu/online（在线）：当前正在运行、可供操作系统调度器分配任务的 CPU；
- /sys/devices/system/cpu/possible（可能上线）：已经被内核分配了资源的 CPU。如果这些 CPU 实际存在，它们就可以被上线。

## linux 中的静态上界，真实 CPU 数量是怎么设计的？
Linux 把「能编多大」和「机器上到底有多少、哪些在跑」拆成多层，而不是只用一个数。
1. 编译期静态上界：NR_CPUS
- 来自 CONFIG_NR_CPUS
- 决定 per-CPU 数组、cpumask 位图等静态分配有多大
- 不等于真实核数；偏大只是多占一点内存（注释里大概每 CPU ~8KB 量级）
2. 启动后发现「可能有哪些」
固件/ACPI（如 MADT）枚举出 APIC ID 等，填入：
- cpu_possible_mask：理论上可能出现的 CPU（含热插拔预留）
- cpu_present_mask：当前硬件上存在的 CPU
- cpu_online_mask：已起来、可调度的 CPU
并有：
- nr_cpu_ids：运行期「可能用到的 CPU 编号上界」（常会从 NR_CPUS 裁小，避免空转大 mask）
- num_possible_cpus() / num_present_cpus() / num_online_cpus()：对上述 mask 计数
真实核数 ≈ present；正在干活的 ≈ online。
3. 启动时再限一次：setup_max_cpus
- Boot 参数 maxcpus=N → 内核里的 setup_max_cpus
- 即使 present 很多，bring-up 也最多拉起 N 个（省电、调试、降内存压力）
```
NR_CPUS          ≥  nr_cpu_ids  ≥  present  ≥  online
（编译能装下）      （编号空间）    （真有）     （在跑）
         ↑
    maxcpus 还可把 online 再压小
```


# SIPI
SIPI 的全称是 Startup Inter-Processor Interrupt（启动处理器间中断）。

在系统刚上电时，通常只有一个主处理器（BSP，Bootstrap Processor）在运行，负责执行 BIOS 和加载操作系统。此时，其余的应用处理器（AP）都处于一种特殊的休眠状态，称为 Wait-for-SIPI（等待 SIPI 状态）。
当 BSP 准备好环境后，会通过高级可编程中断控制器（APIC）向处于休眠状态的 AP 发送 SIPI。这个中断不仅是一个唤醒信号，它还携带了一个关键参数：AP 被唤醒后应该开始执行代码的起始物理地址4。AP 收到 SIPI 后，就会从这个指定的地址开始执行指令，从而完成启动。

## 经典的 INIT-SIPI-SIPI 序列
在实际的操作系统内核代码（如 Linux）中，为了确保兼容性并成功唤醒 AP，BSP 通常会发送一个包含三个 IPI 的标准序列：
- INIT IPI：首先发送一个 INIT 中断，让目标 AP 执行硬件级别的初始化，并进入 Wait-for-SIPI 状态。
- 第一个 SIPI：发送第一个 SIPI，携带启动代码的入口地址，唤醒 AP。
- 第二个 SIPI：紧接着再发送一次 SIPI。这是为了兼容一些较老的处理器架构，确保那些在第一次 SIPI 时可能没有成功响应的 AP 能够被可靠地唤醒。


# BSP 和 AP 是什么意思？
1. BSP (Bootstrapping Processor) - 引导处理器
定义：在多处理器系统中，系统刚上电时，第一个被激活、负责执行 BIOS/UEFI 固件并加载操作系统的 CPU 核心。
职责：BSP 是整个系统的“主心骨”。它负责完成早期的硬件初始化、内存检测、加载操作系统内核等工作。当操作系统启动后，BSP 通常还会承担一些特殊的系统级管理任务。
2. AP (Application Processor) - 应用处理器
定义：系统中除了 BSP 之外的所有其他 CPU 核心。
职责：在系统刚上电时，AP 通常处于休眠或自旋等待状态（例如等待 SIPI 中断）。当 BSP 完成基础环境准备后，会发送信号唤醒 AP。AP 被唤醒并完成自身的初始化后，就会进入空闲状态，等待操作系统的调度器为它们分配任务，与 BSP 一起参与多任务处理和负载均衡。