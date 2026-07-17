# /proc/meminfo 字段详解
```
MemTotal:        7831408 kB
MemFree:         4518804 kB
MemAvailable:    5481128 kB
Buffers:            4224 kB
Cached:          1124776 kB
SwapCached:            0 kB
Active:          1975124 kB
Inactive:         655976 kB
Active(anon):    1512920 kB
Inactive(anon):        0 kB
Active(file):     462204 kB
Inactive(file):   655976 kB
Unevictable:           0 kB
Mlocked:               0 kB
SwapTotal:       2097148 kB
SwapFree:        2097148 kB
Zswap:                 0 kB
Zswapped:              0 kB
Dirty:                 0 kB
Writeback:             0 kB
AnonPages:       1427536 kB
Mapped:           160004 kB
Shmem:             10980 kB
KReclaimable:     129680 kB
Slab:             303316 kB
SReclaimable:     129680 kB
SUnreclaim:       173636 kB
KernelStack:       12272 kB
PageTables:        32628 kB
SecPageTables:         0 kB
NFS_Unstable:          0 kB
Bounce:                0 kB
WritebackTmp:          0 kB
CommitLimit:     6012852 kB
Committed_AS:    2404100 kB
VmallocTotal:   34359738367 kB
VmallocUsed:       39352 kB
VmallocChunk:          0 kB
Percpu:            65536 kB
HardwareCorrupted:     0 kB
AnonHugePages:    364544 kB
ShmemHugePages:        0 kB
ShmemPmdMapped:        0 kB
FileHugePages:     22528 kB
FilePmdMapped:         0 kB
CmaTotal:              0 kB
CmaFree:               0 kB
Unaccepted:            0 kB
Balloon:               0 kB
HugePages_Total:       0
HugePages_Free:        0
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB
Hugetlb:               0 kB
DirectMap4k:      284480 kB
DirectMap2M:     7055360 kB
DirectMap1G:     3145728 kB
```

## 核心内存指标
- MemTotal：系统总可用内存大小（即物理内存减去预留位和内核使用的部分）。
- MemFree：当前系统中完全未使用的空闲内存。
- MemAvailable：估算可供新启动的应用程序使用的内存量。它比 MemFree 更准确，因为它包含了可回收的缓存（如 MemFree + Buffers + Cached）。

## 缓存与缓冲
- Buffers：用于块设备缓冲区的内存大小，主要加速磁盘 I/O 操作。
- Cached：用作页缓存（Page Cache）的内存大小，用于存放已打开文件的数据以加速文件读取。
- SwapCached：已经被交换（Swap）到磁盘但仍在交换缓存中的内存。如果需要，可以很快地被换回内存，避免重复 I/O 操作。

## 活跃与非活跃内存
- Active：最近使用过的内存大小。
- Inactive：最近未使用的内存大小，在内存不足时可能被优先回收。
- Active(anon)：最近使用的匿名内存大小（如进程堆栈、malloc 申请的内存，与文件无关）。
- Inactive(anon)：最近未使用的匿名内存大小。
- Active(file)：最近使用的文件映射内存大小。
- Inactive(file)：最近未使用的文件映射内存大小。

## 锁定与不可回收内存
- Unevictable：由于各种原因无法被换出（回收）的内存大小。
- Mlocked：通过 mlock() 系统调用被锁定在内存中，不会被交换到 Swap 分区的内存大小。

## 交换空间 (Swap)
- SwapTotal：交换空间（Swap 分区/文件）的总大小。
- SwapFree：当前可用的空闲交换空间大小。

## 磁盘回写相关
- Dirty：已修改但等待被写回到磁盘的脏页内存大小。
- Writeback：正在被写回到磁盘的内存大小。

## 用户空间与共享内存
- AnonPages：不属于任何文件的匿名内存页大小。
- Mapped：已被映射到用户空间的内存大小（如设备、文件映射）。
- Shmem：用于共享内存的总大小（包括 tmpfs 等）。

## 内核内存
- KReclaimable：可回收的内核内存大小。
- Slab：内核数据结构缓存的总大小，用于减少频繁申请和释放内存的消耗。
- SReclaimable：Slab 中可回收的部分。
- SUnreclaim：Slab 中不可回收的部分。
- KernelStack：内核堆栈占用的内存大小。
- PageTables：用于管理内存分页的页表（索引表）占用的内存大小。

## 其他与虚拟内存
- NFS_Unstable：NFS 中发送到服务器但尚未写入磁盘的不稳定页面大小。
- Bounce：用于块设备（适配老设备）的临时缓冲区内存大小。
- WritebackTmp：临时用来写回文件系统（如 FUSE）的缓冲区内存大小。
- CommitLimit：根据超额分配比率，系统当前实际可分配内存总量的上限。
- Committed_AS：当前系统已经承诺分配的内存总大小。
- VmallocTotal：虚拟内存（vmalloc 区域）的总大小。
- VmallocUsed：已使用的虚拟内存大小。
- VmallocChunk：最大的连续未被使用的虚拟内存块大小。
- Percpu：每个 CPU 分配的内存大小。

## 硬件与大页内存 (HugePages)
- HardwareCorrupted：系统检测到硬件故障并隔离的损坏内存大小。
- AnonHugePages：用户态透明大页中的匿名内存大小。
- ShmemHugePages：用于共享内存或 tmpfs 的透明大页大小。
- ShmemPmdMapped：使用 PMD 映射到用户空间的共享内存大小。
- HugePages_Total：巨大页的总数。
- HugePages_Free：空闲的巨大页数。
- HugePages_Rsvd：保留的巨大页数。
- HugePages_Surp：额外的巨大页数。
- Hugepagesize：每个巨大页的大小。
- Hugetlb：用于巨大页的内存总大小。

# 直接映射内存
- DirectMap4k：以 4k 大小映射的内存大小。
- DirectMap2M：以 2M 大小映射的内存大小。
- DirectMap1G：以 1G 大小映射的内存大小。