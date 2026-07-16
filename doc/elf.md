# elf 文件和镜像文件的区别
ELF 是“带有丰富元数据的结构化容器”，而镜像文件是“可以直接被 CPU 执行或写入存储设备的纯粹数据流”。

## ELF 文件（Executable and Linkable Format）：
- 包含：
    - 符号表（Symbol Table）：记录了所有函数名和全局变量的名称及其对应的内存地址。
    - 重定位表（Relocation Table）：记录了哪些地址需要在链接或加载时进行调整。
    - 节区头表（Section Header Table）和程序头表（Program Header Table）：描述了文件中各个部分（如 .text, .data, .bss）的属性、大小以及在内存中的映射方式。
- 使用：
    - 编译与链接阶段：编译器生成可重定位的 ELF 文件（.o），链接器将它们合并成可执行的 ELF 文件。
    - 调试阶段：调试器（如 GDB）依赖 ELF 中的符号表将内存地址还原为人类可读的函数名和变量名。
    - 加载阶段：操作系统的加载器（Loader）读取 ELF 的程序头表，知道该把文件的哪一部分加载到内存的哪个位置。
- ELF 文件：体积较大。因为包含了大量的字符串（符号名）和结构体信息。

## 镜像文件（Binary / Image File）：
它通常是扁平的（Flat）、无结构的纯数据。它只包含纯粹的机器码和初始化后的数据。它不包含任何符号名、重定位信息或节区描述。它就像是把 ELF 文件中需要执行的部分“榨干”后，按内存地址顺序拼接在一起的原始字节流。
- 裸机运行（Bare-metal）：在没有操作系统的单片机或嵌入式设备中，Bootloader 直接将镜像文件从 Flash 拷贝到 RAM 然后跳转执行。
- 操作系统内核部署：在 Linux 开发中，编译出的内核通常是 vmlinux（一个标准的 ELF 文件）。但为了启动，构建系统会使用工具（如 objcopy）剥离掉所有调试信息和元数据，生成一个纯粹的 Image 或 zImage 镜像文件，供 Bootloader 加载。
- 镜像文件：体积紧凑。去除了所有非执行必需的信息，通常只保留代码段和已初始化的数据段（未初始化的 .bss 段在镜像中通常不占空间，只在内存中分配）。


# elf 转镜像
objcopy -O binary vmlinux kernel.img
objcopy --set-section-flags .bss=alloc,load,contents -O binary sh.elf sh.img


# elf 详解
- ELF Header（文件头）
- Program Header Table（程序头表）
- Programs（程序区）
- Section Header Table（节区头表）
- Sections（节区）
