# mmap 和 mmap2
- mmap：文件偏移量（offset）的单位是字节（Bytes）。
- mmap2：文件偏移量（pgoffset）的单位是页（Pages），通常固定为 4096 字节。


# mmap 的库函数，使用什么系统调用？
1. 在 32 位架构上：使用 mmap2 系统调用
在 32 位系统（如 x86）中，由于 off_t 类型只有 32 位，最大只能表示约 2GB 的文件偏移，无法满足大文件映射的需求。因此，glibc 的 mmap() 库函数在底层会调用 mmap2 系统调用，并自动将字节级别的偏移量转换为以页（通常为 4096 字节）为单位的偏移量，从而支持高达 16TB 的大文件映射。
2. 在 64 位架构上：使用 mmap 系统调用
在 64 位系统（如 x86-64）中，off_t 类型已经是 64 位，不存在 32 位系统的寻址瓶颈。因此，glibc 的 mmap() 库函数会直接调用 mmap 系统调用，且 64 位架构下根本不存在 mmap2 这个系统调用。


# vma 是什么的简称？
VMA 是 Virtual Memory Area（虚拟内存区域）的简称。


# linux 每进程 mmap 区域数上限
在 Linux 系统中，控制单个进程内存映射（mmap）区域数量上限的参数是 vm.max_map_count。
该参数的默认上限通常为 1048576 或 65536.
cat /proc/sys/vm/max_map_count