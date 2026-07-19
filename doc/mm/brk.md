# brk 和 sbrk
- brk: 系统调用，直接将数据段的结束地址（Program Break）设置为指定的绝对地址；
    原型：int brk(void *addr);
    返回值：成功返回 0，失败返回 -1 并设置 errno
- sbrk: 库函数调用，在当前的 Program Break 基础上增加或减少指定的字节数（增量）；sbrk 全称为 set break.
    原型：void *sbrk(intptr_t increment);
    返回值：成功返回旧的 Program Break 指针，失败返回 (void *)-1 并设置 errno。若传入参数 increment 为 0，则可用于获取当前 Program Break 的位置