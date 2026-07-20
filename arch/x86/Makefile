KDIR=kernel
BOOTDIR=boot
TESTDIR=test
USERDIR=user

TOOLPREFIX :=

CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)as
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

ifndef CPUS
CPUS := 1
endif

MEM ?= 128M

QEMU = qemu-system-x86_64
QEMUOPTS = -machine q35 -m $(MEM) -smp $(CPUS)
# VNC：:N 为 display 号，TCP 端口 = 5900+N；:0 → 0.0.0.0:5900
QEMUOPTS += -vnc 0.0.0.0:0
# QEMUOPTS += -display none	# 仅关闭视频画面
QEMUOPTS += -nographic	# 彻底的纯命令行模式, redirect serial I/Os to console
# QEMUOPTS += -serial stdio -monitor none

# IDE：q35 默认 AHCI，需显式加控制器；驱动按端口表扫描各通道。
# ide0 = piix3-ide：hd0/hd1 @0x1F0，hd2 @0x170。
# ide1 = 第二块控制器：QEMU 第二个 piix3-ide 端口写死会冲突，故用两路
#        isa-ide 模拟双通道扩展卡（0x1E8 / 0x168），挂 hd3/hd4/hd5。
HD_IMG = hd.img
HD1_IMG = hd1.img
HD2_IMG = hd2.img
HD3_IMG = hd3.img
HD4_IMG = hd4.img
HD5_IMG = hd5.img
HD_SIZE_MB ?= 8
QEMUOPTS += -device piix3-ide,id=ide0
QEMUOPTS += -drive file=$(HD_IMG),format=raw,if=none,id=hd0
QEMUOPTS += -device ide-hd,drive=hd0,bus=ide0.0,unit=0
QEMUOPTS += -drive file=$(HD1_IMG),format=raw,if=none,id=hd1
QEMUOPTS += -device ide-hd,drive=hd1,bus=ide0.0,unit=1
QEMUOPTS += -drive file=$(HD2_IMG),format=raw,if=none,id=hd2
QEMUOPTS += -device ide-hd,drive=hd2,bus=ide0.1,unit=0
QEMUOPTS += -device isa-ide,id=ide1a,iobase=0x1e8,iobase2=0x3ee,irq=11
QEMUOPTS += -drive file=$(HD3_IMG),format=raw,if=none,id=hd3
QEMUOPTS += -device ide-hd,drive=hd3,bus=ide1a.0,unit=0
QEMUOPTS += -drive file=$(HD4_IMG),format=raw,if=none,id=hd4
QEMUOPTS += -device ide-hd,drive=hd4,bus=ide1a.0,unit=1
QEMUOPTS += -device isa-ide,id=ide1b,iobase=0x168,iobase2=0x36e,irq=10
QEMUOPTS += -drive file=$(HD5_IMG),format=raw,if=none,id=hd5
QEMUOPTS += -device ide-hd,drive=hd5,bus=ide1b.0,unit=0

CFLAGS := -m32 -ffreestanding -fno-pic -fno-stack-protector -nostdlib -Wall
CFLAGS += -fno-asynchronous-unwind-tables -fno-unwind-tables
USER_CFLAGS := $(CFLAGS) -I$(USERDIR)/include -ffreestanding -fno-builtin

# 用户程序产物（须在 hd.img 规则之前定义，否则依赖展开为空）
USER_LIB_OBJS = \
	$(USERDIR)/lib/start.o \
	$(USERDIR)/lib/usys.o \
	$(USERDIR)/lib/printf.o \
	$(USERDIR)/lib/sleep.o \
	$(USERDIR)/lib/wait.o \
	$(USERDIR)/lib/sbrk.o \
	$(USERDIR)/lib/mmap.o \
	$(USERDIR)/lib/string.o \
	$(USERDIR)/lib/ansi.o \
	$(USERDIR)/lib/termios.o

INITCODE_BIN=$(USERDIR)/init.bin
INITCODE_OBJS=$(USER_LIB_OBJS) $(USERDIR)/init.o
SH_BIN=$(USERDIR)/sh.bin
SH_OBJS=$(USER_LIB_OBJS) $(USERDIR)/sh.o
TEST_ELF=$(USERDIR)/test.elf
TEST_OBJS=$(USER_LIB_OBJS) $(USERDIR)/test.o
FREE_ELF=$(USERDIR)/free.elf
FREE_OBJS=$(USER_LIB_OBJS) $(USERDIR)/free.o

# 打进 hd.img 根目录的文件：host路径[:镜像内名字]
HD_FILES = \
	$(TEST_ELF):test \
	$(FREE_ELF):free

ifeq ($(K),1)
QEMUOPTS += -kernel kernel.elf
else
QEMUOPTS += -drive file=boot.img,format=raw,if=floppy,index=0 -boot a
endif

.PHONY: qemu clean print-cc print-cflags compile_commands.json

qemu: $(HD_IMG) $(HD1_IMG) $(HD2_IMG) $(HD3_IMG) $(HD4_IMG) $(HD5_IMG) \
	$(if $(filter 1,$(K)),kernel.elf,boot.img)
	@$(QEMU) $(QEMUOPTS)

# hd.img：ext2 + 样例 + HD_FILES（任意 host 文件，见 mkhd-ext2.sh）
$(HD_IMG): scripts/mkhd-ext2.sh $(TEST_ELF) $(FREE_ELF)
	@rm -f $@
	@sh scripts/mkhd-ext2.sh $@ $(HD_SIZE_MB) $(HD_FILES)

$(HD1_IMG) $(HD2_IMG) $(HD3_IMG) $(HD4_IMG) $(HD5_IMG):
	@dd if=/dev/zero of=$@ bs=1M count=$(HD_SIZE_MB) status=none
	@echo "created $@ ($(HD_SIZE_MB) MiB)"

$(BOOTDIR)/entry_kernel.o: $(BOOTDIR)/entry.S
	$(AS) --32 -defsym KERNEL=1 $< -o $@

$(BOOTDIR)/setup.o: $(BOOTDIR)/setup.S
	$(AS) --32 $< -o $@

$(BOOTDIR)/entry_bin.o: $(BOOTDIR)/entry.S
	$(AS) --32 $< -o $@

$(BOOTDIR)/boot.o: $(BOOTDIR)/boot.S kernel.bin
	@size=$$(stat -c%s kernel.bin); \
	sectors=$$(( (size + 511) / 512 )); \
	$(AS) --32 -defsym KERNEL_SECTORS=$$sectors $< -o $@; \
	echo "boot.o: KERNEL_SECTORS=$$sectors ($$size byte kernel)"

$(BOOTDIR)/boot.bin: $(BOOTDIR)/boot.o
	@$(LD) -m elf_i386 -Ttext 0x7c00 -e _boot -o boot.elf $^
	@$(OBJCOPY) -O binary -j .text boot.elf $@

C_OBJS = \
	$(KDIR)/main.o \
	$(KDIR)/serial.o \
	$(KDIR)/vga.o \
	$(KDIR)/console.o \
	$(KDIR)/kbd.o \
	$(KDIR)/timer.o \
	$(KDIR)/gdt.o \
	$(KDIR)/idt.o \
	$(KDIR)/interrupt.o \
	$(KDIR)/trapstack.o \
	$(KDIR)/cpu.o \
	$(KDIR)/fpu.o \
	$(KDIR)/lock/spinlock.o \
	$(KDIR)/printf.o \
	$(KDIR)/utils.o \
	$(KDIR)/mm/mem.o \
	$(KDIR)/mm/buddy.o \
	$(KDIR)/mm/slab.o \
	$(KDIR)/mm/vm.o \
	$(KDIR)/mm/mmap.o \
	$(KDIR)/proc.o \
	$(KDIR)/task_queue.o \
	$(KDIR)/init.o \
	$(KDIR)/debug.o \
	$(KDIR)/exception.o \
	$(KDIR)/syscall.o \
	$(KDIR)/sysproc.o \
	$(KDIR)/execve.o \
	$(KDIR)/ipc/signal.o \
	$(KDIR)/fs/inode.o \
	$(KDIR)/fs/namei.o \
	$(KDIR)/fs/ramfs.o \
	$(KDIR)/fs/procfs.o \
	$(KDIR)/fs/ext2.o \
	$(KDIR)/fs/mount.o \
	$(KDIR)/fs/file.o \
	$(KDIR)/fs/pipe.o \
	$(KDIR)/fs/sysfile.o \
	$(KDIR)/block/genhd.o \
	$(KDIR)/block/bio.o \
	$(KDIR)/block/blk-core.o \
	$(KDIR)/ide.o \
	$(TESTDIR)/kernel_test.o \

AS_OBJS = \
	$(KDIR)/trap.o \
	$(KDIR)/trapuser.o \
	$(KDIR)/swtch.o \
	$(KDIR)/userbins.o \

OBJS = $(C_OBJS) $(AS_OBJS)

# 用户程序：user.ld 连续布局（含 .bss）；objcopy 将 .bss 标成可装载并填 0
$(INITCODE_BIN): $(INITCODE_OBJS) $(USERDIR)/user.ld
	@$(LD) -m elf_i386 -T $(USERDIR)/user.ld -o $(USERDIR)/init.elf $(INITCODE_OBJS)
	@$(OBJCOPY) --set-section-flags .bss=alloc,load,contents -O binary $(USERDIR)/init.elf $@

$(SH_BIN): $(SH_OBJS) $(USERDIR)/user.ld
	@$(LD) -m elf_i386 -T $(USERDIR)/user.ld -o $(USERDIR)/sh.elf $(SH_OBJS)
	@$(OBJCOPY) --set-section-flags .bss=alloc,load,contents -O binary $(USERDIR)/sh.elf $@

$(TEST_ELF): $(TEST_OBJS) $(USERDIR)/user.ld
	@$(LD) -m elf_i386 -T $(USERDIR)/user.ld -o $@ $(TEST_OBJS)

$(FREE_ELF): $(FREE_OBJS) $(USERDIR)/user.ld
	@$(LD) -m elf_i386 -T $(USERDIR)/user.ld -o $@ $(FREE_OBJS)

$(USERDIR)/free.o: $(USERDIR)/free.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USERDIR)/init.o: $(USERDIR)/init.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USERDIR)/sh.o: $(USERDIR)/sh.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USERDIR)/test.o: $(USERDIR)/test.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USERDIR)/lib/printf.o: $(USERDIR)/lib/printf.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USERDIR)/lib/sleep.o: $(USERDIR)/lib/sleep.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USERDIR)/lib/wait.o: $(USERDIR)/lib/wait.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USERDIR)/lib/sbrk.o: $(USERDIR)/lib/sbrk.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USERDIR)/lib/mmap.o: $(USERDIR)/lib/mmap.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USERDIR)/lib/string.o: $(USERDIR)/lib/string.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USERDIR)/lib/ansi.o: $(USERDIR)/lib/ansi.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USERDIR)/lib/termios.o: $(USERDIR)/lib/termios.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USERDIR)/lib/usys.o: $(USERDIR)/lib/usys.S
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USERDIR)/lib/start.o: $(USERDIR)/lib/start.S
	$(AS) --32 $< -o $@

$(KDIR)/userbins.o: $(KDIR)/userbins.inc.S $(INITCODE_BIN) $(SH_BIN)
	$(AS) --32 $(KDIR)/userbins.inc.S -o $@

$(KDIR)/trap.o: $(KDIR)/trap.S
	$(AS) --32 $< -o $@

$(KDIR)/trapuser.o: $(KDIR)/trapuser.S
	$(AS) --32 $< -o $@

$(KDIR)/swtch.o: $(KDIR)/swtch.S
	$(AS) --32 $< -o $@

# 语法: 目标列表: 目标模式: 依赖模式
$(C_OBJS): %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# K=1: QEMU -kernel loads ELF at 0x100000 (KERNEL=1 in linker script)
kernel.elf: $(BOOTDIR)/entry_kernel.o $(KDIR)/kernel.ld $(OBJS)
	@$(LD) -m elf_i386 -defsym KERNEL=1 -T $(KDIR)/kernel.ld -e _start -o kernel32.elf $(BOOTDIR)/entry_kernel.o $(OBJS)
	@$(OBJCOPY) -I elf32-i386 -O elf64-x86-64 kernel32.elf $@

# K=0: setup @ 0x7e00 + 内核主体 @ 0x100000（与 K=1 相同链接脚本）
setup.bin: $(BOOTDIR)/setup.o $(KDIR)/setup.ld
	@$(LD) -m elf_i386 -T $(KDIR)/setup.ld -e setup_start -o setup.elf $(BOOTDIR)/setup.o
	@$(OBJCOPY) -O binary -j .text setup.elf $@

kernel_body.elf: $(BOOTDIR)/entry_bin.o $(OBJS) $(KDIR)/kernel.ld
	@$(LD) -m elf_i386 -T $(KDIR)/kernel.ld -e _start -o $@ $(BOOTDIR)/entry_bin.o $(OBJS)

kernel.bin: setup.bin kernel_body.elf
	@$(OBJCOPY) -O binary -j .text -j .rodata -j .data kernel_body.elf kernel_body.bin
	@cat setup.bin kernel_body.bin > $@

boot.img: $(BOOTDIR)/boot.bin kernel.bin
	@dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	@dd if=$(BOOTDIR)/boot.bin of=$@ conv=notrunc status=none bs=1 count=512
	@dd if=kernel.bin of=$@ bs=512 seek=1 conv=notrunc status=none

boot.vmdk: boot.img
	@qemu-img convert -f raw -O vmdk boot.img boot.vmdk

print-cc:
	@echo $(CC)

print-cflags:
	@echo $(CFLAGS)

print-user-cflags:
	@echo $(USER_CFLAGS)

compile_commands.json:
	@python3 scripts/gen_compile_commands.py

clean:
	rm -f *.o *.elf *.bin *.img *.map *.vmdk *.tmp main.s test64.* \
	setup.elf setup.bin kernel_body.elf kernel_body.bin $(HD_IMG) \
	$(BOOTDIR)/*.o $(BOOTDIR)/*.bin \
	$(USERDIR)/*.o $(USERDIR)/*.elf $(USERDIR)/*.bin \
	$(USERDIR)/lib/*.o \
	$(OBJS)
