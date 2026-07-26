# Dentry
Dentry 的设计 100% 属于 VFS 层，具体文件系统完全不参与 dentry 的创建、缓存和管理。

Dentry（Directory Entry，目录项）的核心使命是路径解析（Path Resolution）。当你在用户空间调用 open("/home/user/file.txt") 时，内核需要一层一层地拆解这个字符串，找到对应的 inode。
这个“拆解字符串并缓存结果”的逻辑是所有文件系统通用的。无论是 ext4、XFS 还是 NFS，它们都不关心你用什么字符串来访问文件，它们只关心“请给我 inode 号 12345 的数据块”。因此，把路径解析和目录缓存的逻辑放在 VFS 层，是避免每个具体文件系统重复造轮子的最佳设计。

# 设计
- VFS 负责：分配 dentry 内存、维护全局的 Dentry Cache 哈希表、管理 LRU 回收机制、拼接路径树、处理引用计数。
- 具体文件系统负责：仅仅提供 lookup 函数，告诉 VFS “在这个目录下，这个名字对应哪个 inode”。


# 相关文件
在 Linux 内核源码中，VFS 层关于 dentry（目录项）的核心逻辑主要被拆分到了以下几个关键文件中：
1. fs/dcache.c (核心实现)
这是 dentry 逻辑的绝对核心文件。它包含了 dentry 缓存（dcache）的几乎所有底层实现，主要包括：
- 分配与释放：d_alloc() 用于分配新的 dentry 对象，dput() 用于释放 dentry 并处理引用计数。
- 缓存管理：维护全局的哈希表（用于快速查找）和 LRU 链表（用于内存回收）。
- 状态管理：处理 dentry 的未使用（unused）、正在使用（inuse）和负（negative）状态的转换。
2. fs/namei.c (路径解析)
这个文件负责路径名到 dentry 的解析逻辑。当用户调用 open() 或 stat() 等系统调用时，VFS 需要解析路径字符串：
- 它实现了 link_path_walk() 等核心函数，逐层拆解路径（如 /home/user/file.txt）。
- 在每一层目录中，它会先在 dcache 中查找对应的 dentry，如果找不到（Cache Miss），就会调用具体文件系统的 lookup 方法，然后在此文件中将新找到的 dentry 实例化并挂载到路径树上。
3. include/linux/dcache.h (数据结构定义)
这个头文件定义了 dentry 相关的核心数据结构：
- struct dentry：包含了 d_inode（关联的 inode）、d_parent（父目录）、d_name（文件名）、d_hash（哈希链表指针）、d_lru（LRU 链表指针）等关键字段。
- struct dentry_operations：定义了 dentry 的操作回调函数集（如 d_revalidate、d_hash 等）。