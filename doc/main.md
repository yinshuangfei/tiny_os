# OS 启动流程
main (swapper, pid 0)
  ├─ 硬件/内核子系统初始化（GDT/IDT/PMM/VM/procinit…）
  ├─ rest_init()
  │    ├─ 创建 init (pid 1) → kernel_init → execve(initcode)
  │    └─ 创建 kthreadd (pid 2) → 消费内核线程创建请求
  └─ scheduler()           → 永不返回

# swapper
swapper（pid 0）：仅 boot 阶段借用 PCB，无 kstack、不入就绪队列，不调用 sleep/sched.

# init 进程
init（pid 1）：第一个被调度的内核线程，parent 指向 swapper；随后变为用户态 init.
linux 中，init 由 rest_init 通过 user_mode_thread(kernel_init) 创建。

# kthreadd
kthreadd（pid 2）：内核线程守护进程，parent 指向 swapper.
后续 kthread_create() 在 kthreadd 就绪后入队，由 kthreadd 真正创建，parent 为 kthreadd.

# Linux 里概念
- scheduler（调度器）
    内核代码：schedule()、CFS、runqueue 等，没有 pid
- pid 0（swapper / idle）
    每个 CPU 上的 idle 内核线程，没任务可跑时 CPU 跑它
- pid 1（init/systemd）
    第一个用户态进程，不是调度器
- pid 2（kthreadd）
    所有后续内核线程的创建者
