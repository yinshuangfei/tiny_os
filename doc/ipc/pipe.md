# runcmd 中 fork 两次的逻辑
对 echo a | cat，runcmd 里大致是：

runcmd（管道协调者）
  ├─ fork #1 → 左端：stdout → pipe 写端 → 跑 echo
  ├─ fork #2 → 右端：stdin  ← pipe 读端 → 跑 cat（或继续 runcmd）
  └─ 自己：关掉 pipe 两端，wait 两个孩子


# 管道的两个 fd 在内核指向同一个 inode 吗？
是的，在内核中，管道的读端和写端两个文件描述符（fd）最终指向同一个 inode。
在 Linux 内核中，当你调用 pipe() 时，内核会分配两个文件描述符（比如 3 和 4）。它们的指向关系如下：
- fd 3 (读端) -> file 结构体3 -> dentry -> inode
- fd 4 (写端) -> file 结构体4 -> dentry -> inode
这两个 file 结构体是不同的（因为它们的读写权限、当前偏移量等状态不同），但它们底层的 dentry 和 inode 是完全相同的。