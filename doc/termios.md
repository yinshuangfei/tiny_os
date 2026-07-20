# 名称说明
termios 它的命名来源于 term（Terminal，终端）和 ios（I/O Settings，输入输出设置）的结合。

在现代 Linux 和类 Unix 系统中，termios 代表了一个标准化的终端线路配置结构体（struct termios）。它封装了终端的输入输出控制模式、波特率、数据位、校验位、回显策略等全部运行时参数，是应用程序与底层串口或终端设备进行交互的核心数据契约。

struct termios 是终端编程的核心，它通过四个标志位成员变量（Flags）来分类控制终端的行为。这四个成员分别是 c_iflag、c_oflag、c_cflag 和 c_lflag。


# 核心变量
struct termios 是终端编程的核心，它通过四个标志位成员变量（Flags）来分类控制终端的行为。这四个成员分别是 c_iflag、c_oflag、c_cflag 和 c_lflag。
以下是它们的具体分工和实际编程中最常用的操作：

1. c_iflag (Input Modes - 输入模式)
控制终端在接收数据时的预处理行为。
- IGNCR：忽略接收到的回车符（CR）。
- INLCR：将接收到的换行符（NL）转换为回车符（CR）。
- IXON：启用软件流控。开启后，按下 Ctrl+S 会暂停输出，Ctrl+Q 恢复输出。
2. c_oflag (Output Modes - 输出模式)
控制终端在输出数据时的处理方式。
- OPOST：启用输出处理。如果不设置此位，输出数据将原样发送，不进行任何转换。
- ONLCR：将输出的换行符（NL）自动映射为“回车+换行”（CR-NL）。这也就是为什么在终端里输出 \n 时，光标会自动回到下一行的行首。
3. c_cflag (Control Modes - 控制模式)
这是最常用的标志位，主要用于配置底层硬件参数（如波特率、数据位等）。
- CSIZE (掩码)：用于设置数据位长度。常用的值有 CS8（8位）、CS7（7位）等。
- PARENB：启用奇偶校验。
- CSTOPB：设置两个停止位（不设置则为1个停止位）。
- CREAD：允许接收器读取输入数据。
- CLOCAL：忽略调制解调器状态线。在本地串口通信中必须设置此项，否则程序可能会一直等待载波信号。
4. c_lflag (Local Modes - 本地模式)
控制终端的“行规”特性，比如回显、规范模式等。
- ECHO：启用回显。用户在终端敲入的字符会自动显示在屏幕上。
- ICANON：启用规范（Canonical）模式。在此模式下，输入以行为单位缓冲，必须按下回车键（Enter）后，程序才能通过 read() 读到数据。
- ISIG：启用信号处理。允许 Ctrl+C（SIGINT）、Ctrl+Z（SIGTSTP）等组合键触发相应的中断信号。


# 设计原理
键盘/串口送来的是原始字节流；人对终端的期望却是：
- 打字能看见（回显）
- Backspace 能改当前行
- Enter 才交一整行给程序
- Ctrl+C 杀前台，而不是把 0x03 塞进 read
这些若全塞进每个用户程序，会重复、不一致。Unix 的做法是：在内核（或驱动）里加一层「线路规程 / line discipline」，termios 就是配置这层行为的接口。
```
  键盘/串口 IRQ
        │
        ▼
 ┌──────────────┐
 │  line disc.  │  ← termios 控制这里（规范/raw、回显、信号…）
 │  (行规程)     │
 └──────┬───────┘
        │  read / write
        ▼
   shell / cat / vim
```

## 设计原则
（1）策略与传输分离
硬件只负责收发字节；「要不要回显、要不要等换行、Ctrl+C 是否变信号」是策略，用 termios 开关，而不是写死在键盘驱动里。

（2）默认对人类友好（cooked）
默认接近：ICANON | ECHO | ISIG——行缓冲、回显、产生信号。普通程序（cat、read 一行）几乎零配置就能用。

（3）可切换到程序友好（raw / cbreak）
全屏编辑、游戏、shell 的 readline 需要逐字符、自己画屏幕，于是关掉 ICANON/ECHO，由用户态接管编辑与回显。
原则是：同一块 tty，模式可切换，而不是两套设备。

（4）属性挂在 tty 上，不挂在进程上
termios 是终端对象的状态。前台/后台进程共享同一套设置；shell 在跑子进程前常恢复 cooked，自己 readline 时再切 raw——这是协作约定，不是每进程一份 termios。

（5）用少数正交标志表达组合
c_lflag（本地）、c_iflag（输入）、c_oflag（输出）、c_cflag（硬件）把问题拆开：
回显 ≠ 规范模式 ≠ 信号，可独立组合（例如 cbreak：关 ICANON、留 ECHO）。

（6）控制面与数据面分开
数据：read/write；控制：ioctl(TCGETS/TCSETS) 或 tcgetattr/tcsetattr。
避免把模式字节混进普通读写流。