K=kernel
U=user

OBJS = \
  $K/entry.o \
  $K/start.o \
  $K/main.o \
  $K/printf.o \
  $K/console.o \
  $K/uart.o

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

CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb

# CFLAGS += -MD
# 在任意 2 GiB 窗口内正确寻址全局变量
CFLAGS += -mcmodel=medany
# 独立环境编译,禁止公共符号,不链接标准库,禁用 RISC-V 链接器松弛优化
CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -I.
# CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
# ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
# CFLAGS += -fno-pie -no-pie
# endif
# ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
# CFLAGS += -fno-pie -nopie
# endif

# LDFLAGS = -z max-page-size=4096

.PHONY:
all: qemu

$K/kernel: $(OBJS) $K/kernel.ld
	$(LD) $(LDFLAGS) -T $K/kernel.ld -o $K/kernel $(OBJS)


clean:
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	*/*.o */*.d */*.asm */*.sym \
	$K/kernel fs.img \
	mkfs/mkfs .gdbinit \
	$U/usys.S \
	$(UPROGS)

ifndef CPUS
CPUS := 1
endif

QEMUOPTS = -machine virt -bios none -m 128M -smp $(CPUS) -nographic
QEMUOPTS += -kernel $K/kernel
# QEMUOPTS += -drive file=fs.img,if=none,format=raw,id=x0
# QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

.PHONY:
qemu: $K/kernel
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