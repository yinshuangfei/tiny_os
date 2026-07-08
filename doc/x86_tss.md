# TSS
1. 现代 64 位系统（x86-64）：大部分情况不需要 TSS
2. 传统 32 位系统：必须依赖 TSS
在 32 位 x86 架构下，无论是使用 int 0x80 软中断、sysenter 指令，还是发生硬件中断，只要涉及从 Ring 3 切换到 Ring 0，CPU 硬件层面强制要求必须通过 TSS 获取内核栈指针（ESP0）。
如果没有 TSS，32 位系统根本无法完成特权级切换。
3. 其他 CPU 架构：根本不需要 TSS
TSS 是 Intel x86 架构特有的历史产物。其他架构有完全不同的设计：
- ARM64 (AArch64)：有专门的寄存器（如 VBAR_EL1 和 SPSR_EL1）直接保存内核向量表基地址和栈指针，完全不依赖类似 TSS 的结构。
- RISC-V：通过 stvec 寄存器直接指定内核态入口地址，栈指针直接由软件（汇编代码）在陷入时处理，硬件层面不需要 TSS。

# 内核态 → 用户态（ring 0 → ring 3）
## 第 1 步：在内核栈上构造返回帧
栈顶（iret 弹出顺序）需为：
（地址为低地址到高地址）
| eip        |  用户程序入口
| cs         |  SEG_UCODE | 3  → 0x1B
| eflags     |  通常含 IF=1
| esp        |  用户栈顶
| ss         |  SEG_UDATA | 3  → 0x23
## 第 2 步：恢复寄存器并 iret
- 把返回值写入 trapframe->eax
- 如需修改用户 eip（如 syscall 后 eip += 2 跳过 int 0x80），改 trapframe->eip
- 确认 cs 的 RPL=3、ss 为用户段
- popal、恢复 ds，再 iret
## 第 3 步：iret 硬件行为
- iret 依次弹出：EIP → CS → EFLAGS → ESP → SS（若 CS.RPL=3 才弹 SS/ESP）。
- 弹出后 CPU 回到 ring 3，从用户 eip 继续执行。

# 用户态 → 内核态（ring 3 → ring 0）
触发方式包括：int 0x80、页故障、除零、定时器中断等。CPU 在硬件层面完成大部分工作。
## 第 1 步：硬件自动切栈并压栈
CPU 发现当前 CPL=3、目标门 DPL=0 时：
- 从 TSS 读取 ss0、esp0，切换到内核栈
- 在新内核栈上依次压入
低地址 ← esp
┌─────────┐
│   eip   │  esp+0
│   cs    │  esp+4   (0x1b) ← ecx 读这里
│ eflags  │  esp+8
│ user_esp│  esp+12
│   ss    │  esp+16  (0x23)
└─────────┘
高地址

（地址为低地址到高地址）
| eip        |  ← 用户态下一条指令
| cs         |  ← 用户代码段（如 0x1B = SEG_UCODE|3）
| eflags     |
| esp        |  ← 用户栈指针
| ss         |  ← 用户数据段（如 0x23 = SEG_UDATA|3）
| [err]      |  ← 部分异常带 error code（#GP、#PF 等）
- 从 IDT 取门描述符，加载 CS:EIP 到 ISR 入口
- 若门 DPL < CPL，还会清 IF（关中断）
## 第 2 步：汇编入口保存完整现场
## 第 3 步：C 处理
调用 syscall_handler、page_fault_handler 等，在内核栈上执行。

# 内核态 → 内核态（ring 0 → ring 0）
（内核自己 int 0x80）：
┌─────────┐
│   eip   │  esp+0
│   cs    │  esp+4   (0x08, RPL=0)
│ eflags  │  esp+8
└─────────┘

# TSS 在用户态、内核态切换中的作用
必须用 TSS（至少要有 esp0 和 ss0）。
从 ring 3 进 ring 0 时（中断、int 0x80、页故障等），CPU 会：
- 查当前 TSS（TR 指向的那段）
- 把 ss0、esp0 载入 SS、ESP
- 在该内核栈上压入用户态现场（SS, ESP, EFLAGS, CS, EIP）
- 这是 x86 保护模式的硬件规定，没有别的寄存器或机制可替代。因此要做用户态，就需要 TSS；

和 x86-64 的对比
32 位保护模式：主要靠 esp0/ss0 做 ring3→ring0 切栈
x86-64 长模式：仍要 TSS，但主要用 rsp0 和 IST，不再做硬件任务切换

# push|pop
## 压栈顺序（pusha / pushal）
在 32 位 x86 里，pusha / pushad / pushal 是同一条指令的不同写法，作用一样：把 8 个通用寄存器依次压栈。
从先压到后压：
高地址
┌─────┐
│ EAX │  ← 最先压入
│ ECX │
│ EDX │
│ EBX │
│ ESP │  ← 压栈前的 ESP 值
│ EBP │
│ ESI │
│ EDI │  ← 最后压入，此时 esp 指向这里
└─────┘
低地址

顺序        寄存器           说明
1           EAX             累加器
2           ECX             计数器
3           EDX             数据
4           EBX             基址
5           ESP             压栈之前的 ESP（不是当前 ESP）
6           EBP             栈帧基址
7           ESI             源索引
8           EDI             目标索引
共 8 × 4 = 32 字节。
## popa / popal 的恢复顺序
与压栈相反：
EDI → ESI → EBP → ESP → EBX → EDX → ECX → EAX
注意：popa 会恢复 ESP（用栈里保存的那份），栈指针会跳回 pusha 之前的状态。
## 不包含的寄存器
不保存                           原因
EIP                             由硬件 trap 时压栈
CS / SS / DS / ES / FS / GS     段寄存器，需单独 push
EFLAGS                          硬件 trap 时压栈
CR0~CR4 等控制寄存器             不自动保存
## 16 位 vs 32 位 vs 64 位
指令                模式      寄存器
pusha / popa        16 位     AX, CX, DX, BX, SP, BP, SI, DI
pusha/pushad/pushal 32 位     EAX ~ EDI（上表）
pushaq              64 位     RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI