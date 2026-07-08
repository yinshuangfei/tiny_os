/* 供 loader.S 等汇编 include；与 user/syscall.h 保持一致 */
.set SYS_exit,   2
.set SYS_execve, 3
.set SYS_getpid, 11
.set SYS_write,  16
