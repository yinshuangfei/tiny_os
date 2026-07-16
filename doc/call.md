# cdecl 基本规则（32 位）
项目	    约定
- 参数：从右到左 push 到栈上（注意，靠栈传递参数，而不是寄存器）
- 返回地址：call 时再 push 一条返回地址
- 返回值：整数/指针放 eax（64 位用 eax:edx）
- 栈清理：调用方（caller） 负责 add $N, %esp
- 名字：C 函数名 不加修饰（swtch 就是 swtch）

例如：
```
高地址
  arg2          8(%esp)
  arg1          4(%esp)
  返回地址       (%esp)   ← ESP 指向这里 （最后压，最先出）
低地址
```

寄存器分工（GCC 常用）：
类型	寄存器
- caller-saved（调用方可被破坏）: eax, ecx, edx
- callee-saved（被调函数必须保留）: ebx, esi, edi, ebp, esp

## 位数的对齐
32 位 cdecl 只要求 4 字节对齐；
64 位 System V ABI 要求 16 字节对齐。

## 结构体参数
32 位 cdecl 下，小结构体也可能走栈（传指针更常见）。本项目里 struct trapframe *、struct context * 都是 传 4 字节指针。

## pushal / 中断
进 ISR 后 ESP 已变，不能再靠“进函数时的 4(%esp)”取原始 C 参数；应先把指针存起来（如 movl %esp, %eax 再 pushl %eax 传给 C）。


# C 语言给汇编传参（C 调用汇编）
C 编译器按 cdecl 生成 push + call；汇编函数用 4(%esp)、8(%esp) 取参。
C 侧：
```
swtch(old, new);
```
编译器大致生成：
```
pushl  new        ; 先压第 2 个参数
pushl  old        ; 再压第 1 个参数
call   swtch
addl   $8, %esp   ; caller 清栈（2 个参数 × 4 字节）
```
汇编侧读取参数：
```
movl 4(%esp), %eax	# eax = old，指向 struct context
movl 8(%esp), %edx	# new
```
C 调用汇编需要做的:
- 汇编里 .globl 函数名，与 C 声明同名;
- C 里 extern void foo(...);
- 汇编用 4(%esp)、8(%esp)… 取参数;
- 有返回值时，汇编在 ret 前写 eax;


# 汇编给 C 语言传参（汇编调用 C）
规则一样：汇编扮演 caller，自己 push 参数再 call。
汇编侧：
```
movl %esp, %eax		# eax = trapframe 指针
pushl %eax			# 压第 1 个参数
call syscall_handler
addl $4, %esp		# caller 清栈（1 个参数）
```
C 函数：
```
void syscall_handler(struct trapframe *tf);
```
进入 C 后，编译器认为 tf 在 4(%esp)（此时 ESP 已跳过 call 压入的返回地址）。

汇编调用 C 需要做的
- C 函数在 C 文件里正常定义
- 汇编里 .extern foo
- 从右到左 push 参数
- call foo
- addl $N, %esp 清参数（N = 参数个数 × 4）
- 返回值在 eax，汇编用 movl %eax, ... 读取


# 返回值
- 汇编返回到 → C 调用者：ret 前设 eax
- C返回到 → 汇编调用者：C 的 return x 编译成 mov x, %eax + ret


# 参数个数
汇编调用C语言时，C语言怎么知道有几个参数？
C 运行时并不知道有几个参数——参数个数在编译 C 函数时就定死了，汇编 call 时必须按同一份函数声明压栈/传寄存器，两边靠链接时的符号 + 调用约定对齐，不是 C 在运行时去数栈上有几个值。

核心机制：靠「函数声明」，不靠运行时检测。
```
void syscall_handler(struct trapframe *tf);   // 编译器：1 个参数
```
GCC 编译 syscall_handler 时，生成的代码固定认为：
- 进入函数时，4(%esp) 是第一个参数（cdecl 32 位），没有“数一数 caller 压了几个”的逻辑

汇编必须匹配：
```
pushl %eax              # 压 1 个参数
call syscall_handler
addl $4, %esp           # 清 1 个参数
```
若汇编多压/少压，C 仍会按声明去读 4(%esp)、8(%esp)…… → 栈错乱、读错参数、崩溃。


# 总结
汇编“被”C 调用 vs 汇编“调”C ：两边用的是同一套 cdecl，只是 caller 不同。


# 用户程序启动时的栈空间
以 execve("/mnt/test.elf", {"/mnt/test.elf","hello",0}, {"USER=root",0}) 为例：
```
高地址  USERSTACK = 0x00800000  ─── 栈顶（未映射之上）
        ┌─────────────────────────────┐
        │  "/mnt/test.elf\0"          │  ← 字符串区（信息块）
        │  "hello\0"                  │
        │  "USER=root\0"              │
        ├─────────────────────────────┤
        │  (对齐填充，使下方 %esp      │
        │   16 字节对齐)               │
        ├─────────────────────────────┤
        │  AT_NULL (0)                │  auxv: Auxiliary Vector，辅助向量
        │  0                          │
        ├─────────────────────────────┤
        │  → "USER=root"              │  envp[0]
        │  NULL                       │  envp 结尾
        ├─────────────────────────────┤
        │  → "/mnt/test.elf"          │  argv[0]
        │  → "hello"                  │  argv[1]
        │  NULL                       │  argv 结尾
        ├─────────────────────────────┤
%esp →  │  argc = 2                   │  ← _start 参数读取地址入口
        └─────────────────────────────┘
低地址  … 其余为未用栈空间（可向低址增长）
        USERSTACK-PGSIZE = 0x7FF000  ─── 已映射栈页底
```

_start 如何取参：
```
%esp     →  argc
%esp+4   →  argv[0]   （即 char **argv）
…
%esp+4+4*argc → NULL
再下一字  →  envp[0]   （即 char **envp）
…
再下一字  →  AT_NULL
```
