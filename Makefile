K=kernel
U=user

OBJS = \
  $K/entry.o \
  $K/start.o \
  $K/main.o \
  $K/printf.o \
  $K/console.o \
  $K/uart.o \
  $K/proc.o \
  $K/spinlock.o \
  $K/file.o \
  $K/vm.o \
  $K/string.o \
  $K/kalloc.o \
  $K/trampoline.o \
  $K/trap.o \
  $K/kernelvec.o \
  $K/plic.o \
  $K/bio.o \
  $K/fs.o \
  $K/virtio_disk.o \
  $K/swtch.o \
  $K/sleeplock.o \
  $K/syscall.o \
  $K/sysproc.o \
  $K/log.o \
  $K/sysfile.o \
  $K/exec.o \
  $K/pipe.o \
  $K/debug.o

# Try to infer the correct TOOLPREFIX if not set
ifndef TOOLPREFIX
TOOLPREFIX := $(shell if riscv64-unknown-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
        then echo 'riscv64-unknown-elf-'; \
        elif riscv64-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
        then echo 'riscv64-elf-'; \
        elif riscv64-none-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
        then echo 'riscv64-none-elf-'; \
        elif riscv64-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
        then echo 'riscv64-linux-gnu-'; \
        elif riscv64-unknown-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
        then echo 'riscv64-unknown-linux-gnu-'; \
        else echo "***" 1>&2; \
        echo "*** Error: Couldn't find a riscv64 version of GCC/binutils." 1>&2; \
        echo "*** To turn off this error, run 'gmake TOOLPREFIX= ...'." 1>&2; \
        echo "***" 1>&2; exit 1; fi)
endif

QEMU = qemu-system-riscv64
MIN_QEMU_VERSION = 7.2

CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)gas
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

# -fomit-frame-pointer: 编译器不保留序言
# -fno-omit-frame-pointer: 强制编译器保留完整的序言
CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb

# 让编译器在编译代码的同时，自动生成包含头文件依赖关系的 .d 文件
CFLAGS += -MD
# 在任意 2 GiB 窗口内正确寻址全局变量
CFLAGS += -mcmodel=medany
# 独立环境编译,禁止公共符号,不链接标准库,禁用 RISC-V 链接器松弛优化
CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -I.
# 如果支持 -fno-stack-protector 这个选项，就把它加入到编译参数（CFLAGS）中；
# 如果不支持，就忽略它
# -fno-stack-protector 禁用栈溢出保护机制，操作系统编译中不需要，且禁用它可以提高执行速度
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# Disable PIE when possible
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

# LDFLAGS = -z max-page-size=4096

.PHONY:
all: qemu

$K/kernel: $(OBJS) $K/kernel.ld
	$(LD) $(LDFLAGS) -T $K/kernel.ld -o $K/kernel $(OBJS)
	$(OBJDUMP) -S $K/kernel > $K/kernel.asm
	$(OBJDUMP) -t $K/kernel | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $K/kernel.sym

$U/initcode: $U/initcode.S
	$(CC) $(CFLAGS) -march=rv64g -nostdinc -I. -Ikernel -c $U/initcode.S -o $U/initcode.o
	$(LD) $(LDFLAGS) -N -e start -Ttext 0 -o $U/initcode.out $U/initcode.o
	$(OBJCOPY) -S -O binary $U/initcode.out $U/initcode
	$(OBJDUMP) -S $U/initcode.o > $U/initcode.asm

# 因为当前 OS 没有标准 C 库，用户程序必须链接这些自己实现的基础库才能调用系统调用和
# 打印输出
ULIB = $U/ulib.o $U/usys.o $U/printf.o $U/umalloc.o

$U/usys.S: $U/usys.pl
	perl $< > $@

$U/usys.o: $U/usys.S
	$(CC) $(CFLAGS) -c -o $@ $<

# 用户态程序链接规则 (隐式规则)
# 将各个用户程序的 .o 与用户态基础库 (ULIB) 链接
# _%：表示所有以 _ 开头的文件。当 make 需要生成 user/_ls 时，% 就匹配到了 user/ls
# -N：将代码段和数据段设置为可读、可写、可执行。这在现代操作系统中是不安全的，但在 xv6 这种教学系统中是为了简化内存管理。
# -e main：指定程序的入口点（Entry Point）为 main 函数。这意味着当 xv6 加载这个程序时，会直接跳转到 main 开始执行。
# -Ttext 0：指定代码段（Text segment）在用户空间的起始虚拟地址为 0x0。xv6 的用户程序都是从地址 0 开始加载的。
_%: %.o $(ULIB)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ $^
	$(OBJDUMP) -S $@ > $*.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $*.sym

# $U/_forktest: $U/forktest.o $(ULIB)
# 	# forktest has less library code linked in - needs to be small
# 	# in order to be able to max out the proc table.
# 	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $U/_forktest $U/forktest.o $U/ulib.o $U/usys.o
# 	$(OBJDUMP) -S $U/_forktest > $U/forktest.asm

mkfs/mkfs: mkfs/mkfs.c $K/fs.h $K/param.h
	gcc -Werror -Wall -I. -o mkfs/mkfs mkfs/mkfs.c

# Prevent deletion of intermediate files, e.g. cat.o, after first build, so
# that disk image changes after first build are persistent until clean.  More
# details:
# http://www.gnu.org/software/make/manual/html_node/Chained-Rules.html
.PRECIOUS: %.o $U/%.o

UPROGS = \
    $U/_init \
    $U/_sh \
    $U/_ls
#   $U/_cat \
#   $U/_echo \
#   $U/_forktest \
#   $U/_grep \
#   $U/_kill \
#   $U/_ln \
#   $U/_mkdir \
#   $U/_rm \
#   $U/_stressfs \
#   $U/_usertests \
#   $U/_grind \
#   $U/_wc \
#   $U/_zombie

fs.img: mkfs/mkfs $(UPROGS)
	mkfs/mkfs fs.img $(UPROGS)

-include kernel/*.d user/*.d

clean:
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	*/*.o */*.d */*.asm */*.sym \
	$K/kernel fs.img \
	mkfs/mkfs .gdbinit \
	$U/usys.S $U/initcode $U/initcode.out \
	$(UPROGS)


ifndef CPUS
CPUS := 1
endif

QEMUOPTS = -machine virt -bios none -m 128M -smp $(CPUS) -nographic
QEMUOPTS += -kernel $K/kernel
QEMUOPTS += -drive file=fs.img,if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

.PHONY:
qemu: $K/kernel fs.img
	@$(QEMU) $(QEMUOPTS)

## compile_commands.json 生成工具 start
## 若以后改了 CFLAGS / 换了工具链，在项目根执行：
## make compile_commands.json 来更新 compile_commands.json
.PHONY: print-cc print-cflags compile_commands.json

print-cc:
	@echo $(CC)

print-cflags:
	@echo $(CFLAGS)

compile_commands.json: Makefile gen_compile_commands.py
	python3 gen_compile_commands.py
## compile_commands.json 生成工具 end