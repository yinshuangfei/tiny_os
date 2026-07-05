KDIR=kernel
BOOTDIR=boot
TESTDIR=test

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
QEMUOPTS = -machine q35 -m $(MEM) -smp $(CPUS) -display none
QEMUOPTS += -serial stdio -monitor none

CFLAGS := -m32 -ffreestanding -fno-pic -fno-stack-protector -nostdlib -Wall
CFLAGS += -fno-asynchronous-unwind-tables -fno-unwind-tables

ifeq ($(K),1)
QEMUOPTS += -kernel kernel.elf
else
QEMUOPTS += -drive file=boot.img,format=raw,if=floppy,index=0 -boot a
endif

.PHONY: qemu clean print-cc print-cflags

qemu: $(if $(filter 1,$(K)),kernel.elf,boot.img)
	@$(QEMU) $(QEMUOPTS)

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
	$(KDIR)/mem.o \
	$(KDIR)/serial.o \
	$(KDIR)/timer.o \
	$(KDIR)/interrupt.o \
	$(KDIR)/trapstack.o \
	$(KDIR)/cpu.o \
	$(KDIR)/printf.o \
	$(KDIR)/utils.o \
	$(KDIR)/alloc.o \
	$(KDIR)/vm.o \
	$(KDIR)/debug.o \
	$(KDIR)/exception.o \
	$(TESTDIR)/kernel_test.o \

AS_OBJS = \
	$(KDIR)/trap.o \

OBJS = $(C_OBJS) $(AS_OBJS)

$(AS_OBJS): %.o: %.S
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

clean:
	rm -f *.o *.elf *.bin *.img *.map *.vmdk *.tmp main.s test64.* \
	setup.elf setup.bin kernel_body.elf kernel_body.bin \
	$(BOOTDIR)/*.o $(BOOTDIR)/*.bin \
	$(OBJS)
