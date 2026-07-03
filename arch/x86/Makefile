KDIR=kernel
BOOTDIR=boot

TOOLPREFIX :=

CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)as
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

ifndef CPUS
CPUS := 1
endif

QEMU = qemu-system-x86_64
QEMUOPTS = -machine q35 -m 128M -smp $(CPUS) -display none
QEMUOPTS += -serial stdio -monitor none

CFLAGS := -m32 -ffreestanding -fno-pic -fno-stack-protector -nostdlib -Wall

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

COBJS = \
	$(KDIR)/main.o \
	$(KDIR)/serial.o \
	$(KDIR)/timer.o \
	$(KDIR)/interrupt.o \
	$(KDIR)/printf.o \

OBJS = $(COBJS) $(KDIR)/timervec.o

$(KDIR)/timervec.o: $(KDIR)/timervec.S
	$(AS) --32 $< -o $@

# 语法: 目标列表: 目标模式: 依赖模式
$(COBJS): %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 直接通过链接脚本放至 0x100000 处
kernel.elf: $(BOOTDIR)/entry_kernel.o $(KDIR)/kernel.ld $(OBJS)
	@$(LD) -m elf_i386 -T $(KDIR)/kernel.ld -e _start -o kernel32.elf $(BOOTDIR)/entry_kernel.o $(OBJS)
	@$(OBJCOPY) -I elf32-i386 -O elf64-x86-64 kernel32.elf $@

# 通过 setup 加载器放至 0x7e00 处, 再移动至 0x100000 处
# 这里为什么不直接用 kernel.elf 呢？因为 kernel.elf 包含了 .bss 段，而 .bss 段需要被清零
# -j 会按各段的 VMA（虚拟地址）排布输出：最低地址从 0 偏移开始，中间按地址留空洞（填 0）。
# 所以你现在的 kernel.bin 大小是 8721 字节 = 从 0x7e00 到 .data 末尾 0xa011，和链接布局一致。
kernel.bin: $(BOOTDIR)/setup.o $(BOOTDIR)/entry_bin.o $(OBJS)
	@$(LD) -m elf_i386 -Ttext 0x7e00 -e setup_start -o kernel.tmp $^
	@$(OBJCOPY) -O binary -j .text -j .rodata -j .data kernel.tmp $@

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
	$(BOOTDIR)/*.o $(BOOTDIR)/*.bin \
	$(OBJS)
