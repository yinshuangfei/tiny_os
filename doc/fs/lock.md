# flock（syscall）
全文件级别的锁。


# fcntl（syscall）
支持区域级别的锁（字节范围）。


# lockf（lib）
POSIX 标准的区域锁。
lockf() 并不是一个系统调用，它是 C 标准库（glibc）提供的一个库函数。
在 Linux 中，lockf() 本质上是对 fcntl() 系统调用的封装（wrapper）。