# linux 中，proc/pid 目录是 readdir 时现生成填装的，还是创建进程时就创建的?
在 Linux 里，不是在 fork/clone 时就为每个进程建好 /proc/<pid> 目录树。
实际做法:
- readdir(/proc)：遍历任务表（task list），当场填出各个 pid 目录项；没有一份“所有 pid 目录已挂在 /proc 上”的常驻树。
- lookup(/proc/<pid>/...)：访问时才 instantiate（创建 dentry/inode），确认对应 task 还在才成功。
- 第一次访问后，dentry 可能进 dcache；进程退出时会 invalidate/flush（如 proc_flush_task），避免继续命中已死进程。