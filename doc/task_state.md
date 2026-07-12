# 进程状态
在 Linux 内核中，进程的状态标识主要由进程描述符（task_struct）中的 state 和 exit_state 两个核心字段决定。
```
struct task_struct {
#ifdef CONFIG_THREAD_INFO_IN_TASK
    struct thread_info    thread_info;
#endif
    /* 1. 进程状态：决定进程是否可被调度 */
    unsigned int          __state;

    /* 2. 内核栈：进程进入内核态时使用的栈 */
    void                  *stack;

    /* 3. 任务基础标记与引用计数 */
    atomic_t              usage;
    unsigned int          flags;
    unsigned int          ptrace;

    /* 4. 多核(SMP)相关：记录任务当前所在的 CPU */
    int                   on_cpu;
    unsigned int          cpu;
    int                   recent_used_cpu;
    int                   wake_cpu;

    /* 5. 运行队列与优先级 */
    int                   on_rq;
    int                   prio;
    int                   static_prio;
    int                   normal_prio;
    unsigned int          rt_priority;

    /* 6. 调度器相关实体：包含 CFS 红黑树节点等 */
    const struct sched_class  *sched_class;
    struct sched_entity       se;
    struct sched_rt_entity    rt;
    struct sched_dl_entity    dl;

    /* 7. 调度策略与 CPU 亲和性掩码 */
    unsigned int          policy;
    int                   nr_cpus_allowed;
    cpumask_t             cpus_mask;

    /* 8. 调度统计信息 */
    struct sched_info     sched_info;

    /* 9. 全局任务链表：将系统中所有的 task_struct 串联起来 */
    struct list_head      tasks;

    /* 10. 内存管理：指向进程的虚拟地址空间 */
    struct mm_struct      *mm;
    struct mm_struct      *active_mm;

    /* 11. 进程退出相关状态 */
    int                   exit_state;
    int                   exit_code;
    int                   exit_signal;

    /* 12. 进程标识符 (PID/TGID) */
    pid_t                 pid;
    pid_t                 tgid;

    /* 13. 进程亲缘关系：父进程、子进程、兄弟进程链表 */
    struct task_struct __rcu  *real_parent;
    struct task_struct __rcu  *parent;
    struct list_head      children;
    struct list_head      sibling;
    struct task_struct    *group_leader;

    /* 14. 上下文切换计数与启动时间 */
    unsigned long         nvcsw;
    unsigned long         nivcsw;
    u64                   start_time;
    u64                   start_boottime;

    /* 15. 缺页异常统计 */
    unsigned long         min_flt;
    unsigned long         maj_flt;

    /* 16. 进程名（通常为可执行文件名，最长16字节） */
    char                  comm[TASK_COMM_LEN];

    /* 17. 文件系统信息：当前工作目录、根目录等 */
    struct fs_struct      *fs;

    /* 18. 打开的文件描述符表 */
    struct files_struct   *files;

    /* 19. 信号处理相关 */
    struct signal_struct  *signal;
    struct sighand_struct __rcu    *sighand;
    sigset_t              real_blocked;
    sigset_t              saved_sigmask;
    struct sigpending     pending;

    /* 20. 硬件上下文：保存 CPU 寄存器、程序计数器等状态 */
    struct thread_struct  thread;
};
```

state 字段，其常见的标识（宏定义）及对应值如下：
- TASK_RUNNING (0)：表示进程正在运行或处于就绪状态（在运行队列中等待被调度）。这是进程最活跃的状态。
- TASK_INTERRUPTIBLE (1)：可中断的睡眠状态。进程主动让出 CPU 等待某一条件（如 I/O 完成、管道数据到达），此时可以被信号（如 SIGKILL）中断并唤醒。
- TASK_UNINTERRUPTIBLE (2)：不可中断的睡眠状态。通常用于等待关键的底层资源（如磁盘 I/O），此时进程不响应任何信号，即使发送 kill -9 也无效，必须等资源就绪。
- __TASK_STOPPED (4)：进程被暂停执行，通常是因为收到了 SIGSTOP 等信号。
- __TASK_TRACED (8)：进程被调试器（如 gdb）跟踪和控制，处于暂停状态。

除了 state 字段，内核还通过 exit_state 字段来管理进程的退出生命周期，避免与上述状态产生语义混淆：
- EXIT_ZOMBIE (16)：僵尸状态。子进程已终止，但父进程尚未调用 wait() 回收其退出状态。此时进程不占 CPU，但依然占用内核进程表项。
- EXIT_DEAD (32)：进程的最终状态。父进程已调用 wait()，内核正在清理该进程的 task_struct，此状态极短暂。

补充说明：我们在用户态使用 ps 或 top 命令看到的进程状态（如 R、S、D、T、Z）是内核对上述底层状态做了两层简化后的结果。例如，ps 会将“正在运行”和“就绪排队”统一显示为 R，将不可中断睡眠统一显示为 D.

| 状态 | 32位 Linux 宏定义值 | 64位 Linux 宏定义值 |
| :--- | :--- | :--- |
| TASK_RUNNING | 0 | 0 |
| TASK_INTERRUPTIBLE | 1 | 1 |
| TASK_UNINTERRUPTIBLE | 2 | 2 |
| TASK_STOPPED | 4 | 4 |
| TASK_TRACED | 8 | 8 |
| EXIT_ZOMBIE | 16 | 16 |
| EXIT_DEAD | 32 | 32 |
| TASK_DEAD | 64 | 64 |

state 的简称：
- R: TASK_RUNNING
- D: TASK_UNINTERRUPTIBLE
- T: TASK_STOPPED
- C: TASK_TRACED
- Z: EXIT_ZOMBIE
- E: EXIT_DEAD
- S: TASK_INTERRUPTIBLE

代码中对任务状态的定义：
```
/* Used in tsk->__state: */
#define TASK_RUNNING			0x00000000
#define TASK_INTERRUPTIBLE		0x00000001
#define TASK_UNINTERRUPTIBLE	0x00000002
#define __TASK_STOPPED			0x00000004
#define __TASK_TRACED			0x00000008
/* Used in tsk->exit_state: */
#define EXIT_DEAD				0x00000010
#define EXIT_ZOMBIE				0x00000020
#define EXIT_TRACE				(EXIT_ZOMBIE | EXIT_DEAD)
/* Used in tsk->__state again: */
#define TASK_PARKED				0x00000040
#define TASK_DEAD				0x00000080
#define TASK_WAKEKILL			0x00000100
#define TASK_WAKING				0x00000200
#define TASK_NOLOAD				0x00000400
#define TASK_NEW				0x00000800
#define TASK_RTLOCK_WAIT		0x00001000
#define TASK_FREEZABLE			0x00002000
#define __TASK_FREEZABLE_UNSAFE	(0x00004000 * IS_ENABLED(CONFIG_LOCKDEP))
#define TASK_FROZEN				0x00008000
#define TASK_STATE_MAX			0x00010000
```