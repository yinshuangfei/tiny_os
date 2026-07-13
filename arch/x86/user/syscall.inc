/* 供 loader.S 等汇编 include；与 user/syscall.h 保持一致 */
.set SYS_exit,   1
.set SYS_fork,   2
.set SYS_read,   3
.set SYS_write,  4
.set SYS_waitpid, 7
.set SYS_execve, 11
.set SYS_getpid, 20
.set SYS_nanosleep, 162
