.code16
.set SETUP_BASE, 0x7e00		/* boot.S 把 kernel.bin 读入的临时地址 */
.set STAGE2_LOAD, 0x100000	/* 内核主体链接/运行地址（与 kernel.ld 一致） */
.set BOOT_PARAMS, 0x7DF0	/* boot.S 写入的扇区数，物理地址固定 */
.set CODE_SEL, 0x08
.set DATA_SEL, 0x10
.set ENTRY_OFFSET, 0x17	/* _start - KERNBASE，与 kernel_body.elf 一致 */
.set BOOT_INFO, 0x5000
.set BOOT_INFO_MAGIC, 0x544f4d53	/* 'TOMS' */
.set E820_BUF, 0x5100

/*
 * BIOS E820：遍历可用 RAM 条目，把最大 region end 写入 BOOT_INFO+4，
 * 最后写入 BOOT_INFO_MAGIC。仅处理 base/len 在 32 位地址空间内的区域。
 */
.macro E820_PROBE
	xorw %ax, %ax
	movw %ax, %ds
	movw %ax, %es
	movl $0, (BOOT_INFO + 4)
	xorl %ebx, %ebx

.Le820_loop:
	movl $E820_BUF, %edi
	movl $0x534D4150, %edx		/* 'SMAP' */
	movl $24, %ecx
	movl $0xE820, %eax
	int $0x15
	jc .Le820_done
	cmpl $0x534D4150, %eax
	jne .Le820_done

	cmpl $1, (E820_BUF + 16)	/* type 1 = usable RAM */
	jne .Le820_next
	cmpl $0, (E820_BUF + 4)		/* base 高 32 位须为 0 */
	jne .Le820_next

	movl (E820_BUF + 12), %ecx	/* length 高 32 位 */
	jnz .Le820_4g

	movl (E820_BUF + 8), %eax	/* base_lo + length_lo */
	addl (E820_BUF + 0), %eax
	jmp .Le820_store

.Le820_4g:				/* len_hi==1 且 base/len_lo==0 → 4 GiB */
	cmpl $1, %ecx
	jne .Le820_next
	cmpl $0, (E820_BUF + 0)
	jne .Le820_next
	cmpl $0, (E820_BUF + 8)
	jne .Le820_next
	movl $0xFFFFF000, %eax		/* 32 位页对齐上界 */

.Le820_store:
	cmpl (BOOT_INFO + 4), %eax
	jbe .Le820_next
	movl %eax, (BOOT_INFO + 4)

.Le820_next:
	testl %ebx, %ebx
	jnz .Le820_loop

.Le820_done:
	movl $BOOT_INFO_MAGIC, (BOOT_INFO)
.endm

.section .text
.globl setup_start
.type setup_start, @function	# 告诉调试器 setup_start 是函数

.extern setup_end

# 进入保护模式的标准三步（都在实模式里完成前两步）：
# 实模式                          保护模式
# ─────────────────────────────────────────────
# 1. lgdt gdt_desc     ──→  把 GDT 基址/限长写入 GDTR
# 2. CR0.PE = 1        ──→  CPU 进入保护模式
# 3. ljmp $0x08, ...   ──→  用 GDT 里的代码段描述符刷新 CS，真正跑 32 位代码
#
# 内存布局（kernel.bin = setup.bin + kernel_body.bin）：
#   0x7e00          setup 在此执行
#   setup_end       kernel_body 在低地址缓冲区的起始
#   0x100000        setup 把 kernel_body 搬到此处后 jmp _start

setup_start:
	cli				# BIOS 中断在 IDT 就绪前必须关

	# 使用 Fast A20 Gate 打开 A20，确保 0x100000 不会回绕到 0x00000。
	inb $0x92, %al			# 从 I/O 端口 0x92（Fast A20 Gate）读状态到 AL
	orb $0x02, %al			# 设置 AL 的第 2 位（A20 线使能位）为 1
	outb %al, $0x92			# 使 CPU 能访问 1MB 以上物理内存

	# BIOS E820：探测可用物理内存上界，写入 BOOT_INFO
	E820_PROBE

	# lgdt 必须在进入保护模式之前执行，不能放到 protected_mode 之后
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
	movl $0x90000, %esp		# 临时栈（_start 会再设内核栈）

	# 拷贝 kernel_body：大小 = 磁盘镜像总字节数 − setup 段长度
	movl $BOOT_PARAMS, %ebp
	movzwl (%ebp), %eax
	shll $9, %eax			# sectors * 512
	subl $(setup_end - SETUP_BASE), %eax
	movl $setup_end, %esi		# 源：低地址缓冲区里的 kernel_body
	movl $STAGE2_LOAD, %edi		# 目的：0x100000
	movl %eax, %ecx
	addl $3, %ecx
	shrl $2, %ecx			# rep movsl，长度向上取整到 4 字节
	rep movsl

	movl $(STAGE2_LOAD + ENTRY_OFFSET), %eax
	jmp *%eax			# 跳入 _start（链接地址 0x100017）

	.align 8			# GDT 表 8 字节对齐
gdt:
	.quad 0x0000000000000000	# 空描述符
	.quad 0x00cf9a000000ffff	# 基址 0，限长 4GB，可执行、可读（0x9A = 代码段）
	.quad 0x00cf92000000ffff	# 基址 0，限长 4GB，可读写（0x92 = 数据段）
gdt_end:

# gdt_desc 格式（GDTR 寄存器 48 位，不是表项本身）：
# ┌────────┬────────┬────────┬────────┬────────┬────────┐
# │ Limit  │ Limit  │ Base   │ Base   │ Base   │ Base   │
# │ [7:0]  │ [15:8] │ [7:0]  │ [15:8] │ [23:16]│ [31:24]│
# └────────┴────────┴────────┴────────┴────────┴────────┘
#   字节 0    字节 1    字节 2    字节 3    字节 4    字节 5
#   └─ 16 位 Limit ─┘  └────────── 32 位 Base ──────────┘
gdt_desc:
	.word gdt_end - gdt - 1	# GDT 表长度，-1 因为 limit 从 0 计
	.long gdt
