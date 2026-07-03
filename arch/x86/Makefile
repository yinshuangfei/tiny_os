KDIR=.
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

print-cc:
	@echo $(CC)

print-cflags:
	@echo $(CFLAGS)

qemu: $(if $(filter 1,$(K)),kernel.elf,boot.img)
	$(QEMU) $(QEMUOPTS)

entry_kernel.o: entry.S
	$(AS) --32 -defsym KERNEL=1 $< -o $@

loader.o: loader.S
	$(AS) --32 $< -o $@

entry_bin.o: entry.S
	$(AS) --32 $< -o $@

boot.bin: boot.S
	$(AS) --32 $< -o boot.o
	$(LD) -m elf_i386 -Ttext 0x7c00 -e _boot -o boot.elf boot.o
	$(OBJCOPY) -O binary -j .text boot.elf $@

OBJS = \
	$(KDIR)/main.o \
	$(KDIR)/serial.o \

# 语法: 目标列表: 目标模式: 依赖模式
$(OBJS): %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 直接通过链接脚本放至 0x100000 处
kernel.elf: entry_kernel.o kernel.ld $(OBJS)
	$(LD) -m elf_i386 -T kernel.ld -e _start -o kernel32.elf entry_kernel.o $(OBJS)
	$(OBJCOPY) -I elf32-i386 -O elf64-x86-64 kernel32.elf $@

# 通过 loader 加载器放至 0x7e00 处, 再移动至 0x100000 处
kernel.bin: loader.o entry_bin.o $(OBJS)
	$(LD) -m elf_i386 -Ttext 0x7e00 -e loader_start -o kernel.tmp $^
	$(OBJCOPY) -O binary -j .text -j .rodata kernel.tmp $@
	@echo "kernel.bin size: $$(wc -c < $@) bytes"

boot.img: boot.bin kernel.bin
	@dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	@dd if=boot.bin of=$@ conv=notrunc status=none bs=1 count=512
	@dd if=kernel.bin of=$@ bs=512 seek=1 conv=notrunc status=none

boot.vmdk: boot.img
	@qemu-img convert -f raw -O vmdk boot.img boot.vmdk

clean:
	rm -f *.o *.elf *.bin *.img *.map *.vmdk *.tmp main.s test64.*
