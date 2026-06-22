# Depend
https://pdos.csail.mit.edu/6.S081/2020/tools.html
https://github.com/mit-pdos/xv6-riscv
https://pdos.csail.mit.edu/6.S081/2020/xv6/book-riscv-rev1.pdf

## Configure and build the toolchain
```
$ cd ../
$ git clone --recursive https://github.com/riscv/riscv-gnu-toolchain

$ cd riscv-gnu-toolchain
$ ./configure --prefix=/usr/local
$ sudo make
$ cd ..
```

## Direct install
```
wget https://static.dev.sifive.com/dev-tools/freedom-tools/v2020.08/riscv64-unknown-elf-gcc-10.1.0-2020.08.2-x86_64-linux-ubuntu14.tar.gz
tar -xzf riscv64-unknown-elf-gcc-10.1.0-2020.08.2-x86_64-linux-ubuntu14.tar.gz
mv riscv64-unknown-elf-gcc-10.1.0-2020.08.2-x86_64-linux-ubuntu14 riscv-gcc-10.1.0

export PATH=$PATH:/path/to/riscv-gcc-10.1.0/bin

$ riscv64-unknown-elf-gcc --version
riscv64-unknown-elf-gcc (GCC) 10.1.0
...
```

## Build QEMU 5.1.0
```
$ wget https://download.qemu.org/qemu-5.1.0.tar.xz
$ tar xf qemu-5.1.0.tar.xz

$ cd qemu-5.1.0
$ ./configure --disable-kvm --disable-werror --prefix=/usr/local --target-list="riscv64-softmmu"
$ make
$ sudo make install
$ cd ..

$ qemu-system-riscv64 --version
QEMU emulator version 5.1.0
```

## Run the OS
```
$ git clone git://g.csail.mit.edu/xv6-labs-2020
$ cd xv6-labs-2020
$ git checkout util
$ make qemu
```

## Conmand
- ps: xv6 has no ps command, if can type Ctrl-p.
- exit: To quit qemu type: Ctrl-a x.

## Qemu CMD
- quit: Ctrl a + x
- cmd: Ctrl a + c