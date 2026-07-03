.code16
.set STAGE2_TMP, 0x7e00
.set STAGE2_LOAD, 0x100000
.set BOOT_PARAMS, 0x7DF0
.set CODE_SEL, 0x08
.set DATA_SEL, 0x10

.section .text
.globl setup_start
.type setup_start, @function	# 告诉调试器 _start 是函数

# 进入保护模式的标准三步（都在实模式里完成前两步）：
# 实模式                          保护模式
# ─────────────────────────────────────────────
# 1. lgdt gdt_desc     ──→  把 GDT 基址/限长写入 GDTR
# 2. CR0.PE = 1        ──→  CPU 进入保护模式
# 3. ljmp $0x08, ...   ──→  用 GDT 里的代码段描述符刷新 CS，真正跑 32 位代码

# 目前的地址是 boot.S 加载至的 0x7e00
setup_start:
	# 使用 Fast A20 Gate 打开 A20，确保 0x100000 不会回绕到 0x00000。
	inb $0x92, %al			# 从 I/O 端口 0x92（Fast A20 Gate）读状态到 AL
	orb $0x02, %al			# 设置 AL 的第 2 位（A20 线使能位）为 1
	outb %al, $0x92			# CPU 能够真正访问 1MB 以上的物理内存，寻址 46 位，寻址空间为 64 TB

	# lgdt 必须在进入保护模式之前执行，不能放到 protected_mode 标签之后。核心原因是：要先有 GDT，才能用段选择子跳进 32 位代码
	lgdt gdt_desc			# 加载 GDT 描述符

	# 读 CR0 → 置 bit 0（PE，Protection Enable）→ 写回，开启保护模式
	movl %cr0, %eax
	orl $0x1, %eax
	movl %eax, %cr0

	# 远跳转到 32 位代码段，刷新流水线，真正进入保护模式
	ljmp $CODE_SEL, $protected_mode

.code32		# 告诉编译器接下来的代码是 32 位模式（保护模式）
protected_mode:
	# 进入 32 位保护模式后，先建立平坦段寄存器和栈。
	movw $DATA_SEL, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	movw %ax, %fs
	movw %ax, %gs
	movl $0x90000, %esp		# 栈指针设置

	# 复制长度来自 MBR boot_params（boot.S 在汇编期写入，物理地址 0x7DF0）
	movl $BOOT_PARAMS, %ebp
	movzwl (%ebp), %eax
	shll $7, %eax			# sectors * 512 / 4
	movl %eax, %ecx
	movl $STAGE2_TMP, %esi
	movl $STAGE2_LOAD, %edi
	rep movsl

	movl $STAGE2_LOAD, %eax
	addl $(_start - setup_start), %eax
	jmp *%eax

	.align 8			# GDT 表是 8 字节对齐的
gdt:
	.quad 0x0000000000000000	# 空描述符
	.quad 0x00cf9a000000ffff	# 基址 0，限长 4GB，可执行、可读（0x9A = 代码段属性）
	.quad 0x00cf92000000ffff	# 基址 0，限长 4GB，可读写（0x92 = 数据段属性)
gdt_end:

# gdt_desc 地址
# ┌────────┬────────┬────────┬────────┬────────┬────────┐
# │ Limit  │ Limit  │ Base   │ Base   │ Base   │ Base   │
# │ [7:0]  │ [15:8] │ [7:0]  │ [15:8] │ [23:16]│ [31:24]│
# └────────┴────────┴────────┴────────┴────────┴────────┘
#   字节 0    字节 1    字节 2    字节 3    字节 4    字节 5
#   └─ 16 位 Limit ─┘  └────────── 32 位 Base ──────────┘
gdt_desc:				# GDT 伪描述符，不是 GDT 表项本身
	.word gdt_end - gdt - 1		# GDT 表长度，-1 是因为 GDT 表从 0 开始
	.long gdt
