# Depend
https://pdos.csail.mit.edu/6.S081/2020/tools.html

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