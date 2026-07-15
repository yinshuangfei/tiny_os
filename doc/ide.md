# IDE 控制器
IDE 控制器寄存器的挂载位置取决于系统架构以及 IDE 控制器的总线类型（传统 ISA/PCI 或现代 PCIe）。主要分为以下三种情况：

## 1.传统 x86 架构（I/O 端口映射）
在传统的 x86 计算机中，IDE 控制器寄存器通常不占用物理内存地址，而是映射到独立的 I/O 地址空间（通过 in 和 out 指令访问）。
- Primary（主）通道：
    - 数据/命令寄存器：0x1F0 - 0x1F7
    - 控制/状态寄存器：0x3F6 - 0x3F7
- Secondary（从）通道：
    - 数据/命令寄存器：0x170 - 0x177
    - 控制/状态寄存器：0x376 - 0x377

## 2.内存映射 I/O (MMIO) 模式
在现代系统或嵌入式设备中，如果 IDE 控制器（如 PCI IDE 控制器）被配置为内存映射模式，其寄存器会被映射到系统物理内存地址空间中。
- 具体位置：由系统的 PCI 总线枚举过程动态分配。
- 如何获取：操作系统在启动时会读取 PCI 配置空间中的 BAR (Base Address Register) 寄存器（通常是 BAR0 到 BAR5）。BAR 寄存器返回的物理地址，就是该 IDE 控制器寄存器在内存中的确切起始位置。

## 3.ARM 等嵌入式架构
在 ARM 等非 x86 架构中，IDE 控制器几乎总是采用内存映射 I/O (MMIO)。
- 具体位置：由硬件设计（SoC 数据手册）或设备树（Device Tree）静态定义。例如，某个 ARM 芯片的数据手册可能会明确规定其 IDE 寄存器基地址为 0x40000000。


# IDE 控制器特点
IDE（Integrated Drive Electronics，电子集成驱动器）是早期个人电脑中用于连接存储设备与主板的标准接口技术，也称为ATA接口。
一个 IDE 驱动器包含两个 IDE 接口（即两个通道），每个通道可以连接两个IDE设备，通过跳线或电缆来区分主设备（Master）和从设备（Slave）。

## 访问模式
IDE设备的访问模式主要涉及两个层面：数据传输模式（Transfer Mode）和硬盘寻址模式（Access Mode）。
### （1）数据传输模式（Transfer Mode）
- PIO（Programmed I/O）模式：最早期的数据传输模式。通过执行I/O端口指令进行数据的读写。这种模式传输速率低下（3.3MB/s至16.6MB/s），CPU占用率极高，大量传输数据时容易导致系统停顿，现已基本被淘汰。
- DMA（Direct Memory Access）模式：直接内存访问模式。不经过CPU，而是由DMA控制器直接从内存中存取数据。CPU下达指令并在传输完毕后接收反馈，大大减轻了CPU的资源占有率。最大传输速率可达16.6MB/s。
- Ultra DMA（UDMA）模式：DMA模式的增强版本。它在包含DMA优点的基础上，增加了CRC（循环冗余校验）技术以提高数据传输的准确性和安全性。UDMA 应用了双倍数据传输（DDR）技术，使传输速度成倍增长，最高可达100MB/s甚至133MB/s。
### （2）硬盘寻址模式（Access Mode）
- CHS（Cylinder, Head, Sector）：传统的柱面-磁头-扇区寻址方式，通常仅支持最大528MB容量的硬盘。
- LBA（Logical Block Addressing）：逻辑块寻址模式。它将物理的柱面、磁头和扇区转换为逻辑地址，突破了504MB/528MB的容量限制，可支持最高达8.4GB的硬盘。
- LARGE：当硬盘的柱面数超过1024且操作系统不支持LBA模式时使用的折中模式。
- Auto：系统自动检测并选择最合适的寻址模式，这是推荐的默认设置。

## 一个 IDE 控制器最多可连接几个设备
一条 IDE 通道（一根 40/80 线排线），连接 2 个设备（master / slave）。
一个 IDE 控制器（主通道 + 次通道），连接 4 个设备（2 通道 × 2）。
对应端口：
- 主通道：0x1F0 / 0x3F6，IRQ14
- 次通道：0x170 / 0x376，IRQ15

## 多个 IDE 控制器
每个控制器最多 4 盘（2 通道 × master/slave），总盘数是各控制器之和，但端口和中断不能共用同一套约定。
其他 IDE 控制器（除了第一个）连接方法
### （1）PCI IDE / 兼容模式
第 1 块仍用 0x1F0/0x170；后续控制器由 PCI BAR 提供 另一组 I/O 基址（例如 0x1E8/0x168，或完全由 BIOS/固件分配）。
### （2）PCI Native 模式
主/次通道端口都来自 PCI 配置空间的 BAR，不再固定是 1F0/170。驱动必须先枚举 PCI，读出每个功能的 BAR，再按“控制器 → 通道 → master/slave”探测。
### （3）中断
兼容模式常仍是 IRQ14/15；多控制器或 native 模式多用 PCI IRQ / MSI，不能假定还是 14/15。
### 系统拓扑
系统
 ├─ IDE 控制器 0  →  通道0/1  →  每通道 master+slave （最多 4）
 ├─ IDE 控制器 1  →  通道0/1  →  每通道 master+slave （最多 4）
 └─ ...


# 疑问解答
### ide 控制器上游连接什么设备？
```
CPU
└─ 北桥 / 内存控制器（较老）或 CPU 直连
     └─ PCI / PCIe 总线
          └─ IDE 控制器（南桥里的一块，或独立 PCI 卡）
               ├─ 主通道 0x1F0  →  master / slave 硬盘、光驱…
               └─ 次通道 0x170  →  master / slave …
```
QEMU/piix3-ide 的写法：
```
CPU ── PCI ── PIIX3（南桥）── IDE 功能（piix3-ide）
                             ├─ ide.0（primary）
                             |   ├─ unit0 (master)
                             |   └─ unit1 (slave)
                             └─ ide.1（secondary）
                                 ├─ unit0 (master)
                                 └─ unit1 (slave)
```
## PCI 向 CPU 延伸，连接什么器件？
PCI 朝 CPU 方向不是直接焊在 CPU 引脚上，中间有一层 主机桥（Host Bridge）/根复合体（Root Complex）。

经典 PC（和 QEMU 的 i440FX + PIIX 很像）
```
CPU
 └─ 前端总线 FSB（较老）
      └─ 北桥（Host Bridge / MCH）
           ├─ 内存
           ├─ AGP/显卡（若有）
           └─ PCI 总线 ──► 南桥（PIIX）──► IDE、USB、中断控制器…
```
- CPU ↔ 北桥：处理器总线（FSB 等）
- 北桥：PCI 的上游入口，也叫 PCI Host Bridge
- PCI 总线本身：挂南桥、网卡、声卡等
- PIIX / piix3-ide：PCI 下游设备

较新的写法（PCIe 时代）
```
CPU（内含 Root Complex）
 └─ PCIe Root Port
      └─ 设备 / 桥
           └─（若有）PCI 兼容桥 → 老式 PCI 设备
```
很多功能已进 CPU，Root Complex ≈ 以前的 Host Bridge。

## Host Bridge 具体是什么东西？
Host Bridge（主机桥） 是把 CPU/内存一侧和 PCI 总线一侧接起来的桥片逻辑。
```
CPU 地址空间（内存、端口）
        ↕
   Host Bridge   ← 就在这里做转换/转发
        ↕
     PCI 总线     （设备、配置空间、BAR）
```
- 对 CPU：像一段普通的内存/IO 访问路径
- 对 PCI：是这条 PCI 总线的主桥 / 起点（bus 0 通常从它长出来）
在 PCI 枚举里，它往往表现为 Bus 0 上的一个 PCI 设备（常见是 00:00.0），类型是 Host Bridge（class 0x06 / subclass 0x00）。
### 它具体干什么
(1) 地址转发
CPU 访问某段 MMIO 或 I/O 口 → Host Bridge 判断该不该转到 PCI，再发给对应设备。
(2) 配置空间入口
软件读 0xCF8/0xCFC（或 ECAM）访问 PCI 配置空间，通常也经 Host Bridge 这条路径下去。
(3) 分出 PCI 总线
后面的南桥、网卡、IDE 控制器等都挂在它分出去的 PCI（或再经 PCI-PCI Bridge 延伸）。
(4) 和内存子系统协作（经典北桥）
老平台上 Host Bridge 常和内存控制器做在同一颗北桥里：一边管 DRAM，一边管 PCI。
### 它“长”在哪
- 老 PC: 北桥芯片里（如 i440FX）
- QEMU pc/i440fx: 模拟的 i440FX Host Bridge（00:00.0）
- 现代 PC / QEMU q35: 多在 CPU 的 Root Complex 里（PCIe 的等价物）

## CPU 怎么访问 Host Bridge？
CPU 一般不把 Host Bridge 当成“再 out 一个特殊端口的神秘芯片”来用；它是通过‘固定的配置空间入口’和‘普通的内存/IO 访问’够到它的。
### (1) 最常见：PCI 配置空间
x86 上经典入口是两个 IO 口（南桥/芯片组提供，经 Host Bridge 路径生效）：
- 0xCF8：
    地址口：写下要访问的 bus/device/function/register
- 0xCFC：
    数据口：读/写那 4 字节配置数据

Host Bridge 自己通常就是：Bus 0, Device 0, Function 0   →   00:00.0
读 Vendor/Device ID：
```
1. 拼配置地址：bus=0, dev=0, fn=0, reg=0
2. outl(地址, 0xCF8)
3. inl(0xCFC)  →  得到厂商/设备 ID
```
### (2) “间接”用它
多数时候内核并不是一直在“操作 Host Bridge”，而是：
```
CPU 执行 inl/inb/mov 访问某段 IO/MMIO
    → Host Bridge 看地址属不属于 PCI
    → 转发到对应 PCI 设备（例如 IDE 的 0x1F0）
```
### (3) 访问路径
```
CPU
 └─ 总线周期（IO / 内存 / 配置）
      └─ Host Bridge（解码、转发、或响应自己是 00:00.0）
           ├─ 命中自身配置寄存器 → 返回桥自己的信息
           └─ 命中 PCI 下游地址 → 转到 piix3-ide 等设备
```

## CPU 怎么区分 IO/MMIO？
在 x86 上，靠 CPU 发出的总线周期类型不同 ———— 由指令决定走 IO 空间还是内存空间。
(1) I/O 空间
- 访问指令：in / out（及 ins/outs）
- 地址宽度：典型 16 位端口（0x0000–0xFFFF）
- 例子：inb(0x1F0)、0xCF8
(2) 内存空间（含 MMIO）
- 访问指令：mov、读写内存的一切普通访存
- 地址宽度：线性/物理地址（32/64 位）
- 例子：显存、PCI BAR 映射的寄存器

同一个数字（比如 0x1000）可以：
- 作为 IO 端口 0x1000
- 又作为 物理内存/MMIO 地址 0x1000
二者不相干，Host Bridge/芯片组按事务类型分别解码。

流水线/总线侧会带上属性：
- I/O 周期：访问 I/O 地址空间
- Memory 周期：访问内存地址空间（DRAM 或被映射成寄存器的 MMIO）

### MMIO 为什么叫“Memory-Mapped”
外设寄存器被 解码进物理内存地址。CPU 仍用普通 mov：
```
CPU: mov (某虚拟地址)
  → MMU 译成物理地址
  → Host Bridge/北桥看：这段是 DRAM 还是设备？
       ├─ DRAM → 进内存控制器
       └─ MMIO → 转到 PCI/设备
```

## MMU 在 CPU 内部吗？
对，MMU 在 CPU 内部（现代 x86 都是片上集成，不是主板上一颗独立芯片）。
- 逻辑上：MMU 属于 CPU 的地址翻译单元（和 TLB、页表基址寄存器如 CR3 一起）。
- 物理上：做在同一颗 CPU 里；主板上不会再单独焊一颗 “MMU 芯片”。

当代主板（CPU 集成内存控制器 + Root Complex），Host Bridge 在这里对应片内的 Root Complex / System Agent 一侧，不经过 MMU：
```
核心 ──mov──► MMU ──► Uncore ─┬─► IMC ────────► DDR
                              └─► Root Complex ► PCI（MMIO）

核心 ──in/out──────────────────────────► Root Complex ► PCI（Port I/O，如 0x1F0）
         ▲
         └── 不经过 MMU
```

QEMU i440FX + piix3-ide 那种老拓扑，Host Bridge 不在 CPU 内，应画成：
```
CPU（含 MMU）──FSB──► 北桥 Host Bridge ──┬──► DDR
                                        └──► PCI ──► PIIX/IDE ──► 硬盘
```

## IDE 控制器上的硬盘，可以出现不连续连接吗?
可以。
可以只有 slave、没有 master（逻辑上“不连续”），只是不如“只挂 master”稳。

## IDE 地址为什么是 0x1f0？
0x1F0 不是 CPU 或 PCI 算出来的，而是 IBM PC/AT 兼容机约定死的 ISA I/O 地址，后来被 ATA/IDE 规范当成“兼容模式”主通道基址沿用下来。
- IBM PC/AT 硬件/BIOS 传统：把第一块硬盘控制器放在固定 IO 口
- ATA/IDE 规范（兼容模式）：明文规定：Primary = 0x1F0–0x1F7，控制口 0x3F6
- 芯片组 / QEMU piix3-ide：在 legacy 模式下 硬接线 到这些口，不靠 BAR 分配

## ISA I/O 地址有哪些？
ISA I/O 地址与系统内存地址（RAM）是完全隔离的。CPU 通过专门的指令（如 IN 和 OUT）来读写这些地址，而不是用读写内存的指令。
早期的 8 位 ISA 总线使用 10 根地址线，提供 1024 个 I/O 端口；16 位 ISA 总线扩展到了 16 根地址线，因此其 I/O 地址空间为 0x0000 到 0xFFFF，总共 65,536 个端口。
### 地址全集
注意事项：
- x86 in/out 端口空间：0x0000–0xFFFF（16 位）
IBM AT 技术手册约定：
- 0x000–0x0FF：主板保留
- 0x100–0x3FF：I/O 通道（插卡可用）
### IBM PC/AT 官方 I/O 图（核心“标准全集”）
https://stanislavs.org/helppc/ports.html
http://www.techhelpmanual.com/892-i_o_port_map.html
https://bitsavers.trailing-edge.com/pdf/ibm/pc/at/1502494_PC_AT_Technical_Reference_Mar84.pdf

(1) 主板（约 000–0FF）
- 0x00 - 0x1F：DMA 控制器 1（8237）
- 0x20 - 0x3F：PIC-1, 中断控制器 1（8259 主片）
- 0x40 - 0x5F：系统定时器（PIT, 8253/8254）
- 0x60 - 0x6F：键盘控制器 (8042)
- 0x70 - 0x7F：CMOS / 实时时钟 (RTC + NMI mask)
- 0x80 – 0x9F: DMA page register
- 0xA0 – 0xBF: PIC-2, 中断控制器 2（8259 从片）
- 0xC0 – 0xDF: DMA 控制器 2
- 0xF0 – 0xFF: 数学协处理器相关
(2) 通道/适配器（约 100–3FF）
- 0x170 - 0x177: 178-17F 保留，固定硬盘-1 (Secondary IDE/ATA, Secondary 通道, 可接 2 个硬盘)
- 0x1F0 – 0x1F7: 1F8-1FF 保留，固定硬盘-0 (Primary IDE/ATA, Primary 通道, 可接 2 个硬盘)
- 0x200 – 0x207: Game I/O
- 0x220：经典的 Sound Blaster 声卡默认地址
- 0x278 – 0x27F: 并行口 2（LPT2）
- 0x2F8 – 0x2FF: 串口 2（COM2）
- 0x300 – 0x31F: Prototype / 实验卡区
- 0x360 – 0x36F: 保留（后来常被网卡等占用）
- 0x378 – 0x37F: 并行口 1（LPT1）
- 0x380 – 0x38F: SDLC / 同步通信 2
- 0x3A0 – 0x3AF: 同步通信 1
- 0x3B0 – 0x3BF: MDA 单显 + 打印机
- 0x3C0 – 0x3CF: 保留（后为 EGA/VGA）
- 0x3D0 – 0x3DF: CGA
- 0x3F0 – 0x3F7: 软盘控制器
- 0x3F8 – 0x3FF: 串口 1（COM1）
(3) 后来成为“事实标准”的补充（原 AT 表未写全或后补）
- 0x376: Secondary IDE
- 0x3F6: Primary IDE 设备控制/alt status（与软盘区交错，历史上有名）
- 0x3E8 – 0x3EF: COM3
- 0x2E8 – 0x2EF: COM4
- 0x1E8 / 0x168 一带: 第三/第四 IDE（扩展卡，非最早 AT 表）
- 0x220 – 0x22F 等: Sound Blaster 等声卡（厂商常用）
- 0x3BC – 0x3BE: 另一组并口（MDA 卡上）
### ISA I/O 地址的痛点
- 资源冲突：由于地址是固定的，如果用户安装了两块声卡且都跳线设置为 0x220，系统就会崩溃或设备失效。
- 缺乏即插即用：早期 ISA 设备被称为“跳线地狱”，用户必须手动查阅手册，通过拨码开关或短接帽来配置 I/O 地址、IRQ 和 DMA 通道。直到后期的 PnP ISA 规范出现，才稍微缓解了这个问题。
### 历史演进
随着硬件越来越复杂，ISA 总线带宽太低（仅 8MB/s），且配置繁琐，最终被 PCI 总线取代。PCI 引入了自动配置（BAR 寄存器分配 I/O 和内存空间），彻底消灭了手动设置 I/O 地址的时代。不过，为了向后兼容，现代主板的底层芯片组（南桥/PCH）内部依然保留着这些经典的 ISA I/O 端口映射。
### 当前 q35 模拟器结构
-machine q35, 是 Q35 + ICH9，主总线是 PCIe/PCI，不是 ISA 插槽机。
```
CPU
 └─ PCIe root (q35)
      ├─ piix3-ide、网卡等（PCI 设备）
      └─ ICH9-LPC（PCI 上的 ISA bridge）
           └─ isa.0  ← 兼容用的 ISA 总线
                ├─ 8259 PIC
                ├─ 8254 PIT
                ├─ 串口 / 蜂鸣器 …
                └─ 你们加的 isa-ide（第二块 IDE）
```
QEMU 的 isa.0 模拟的是 PC/AT 那一代 ISA，即 16 位 ISA.