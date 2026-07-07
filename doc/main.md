# OS 启动流程
main (swapper, pid 0)
  ├─ 硬件/内核子系统初始化（GDT/IDT/PMM/VM/procinit…）
  ├─ init_start()          → 创建 init 内核线程 (pid 1)
  └─ scheduler()           → 永不返回
init (pid 1)
  ├─ kernel_test()         → 测试在 scheduler 管理下运行
  └─ sleep_ticks 循环      → init 常驻，不退出

# swapper
swapper（pid 0）：仅 boot 阶段借用 PCB，无 kstack、不进就绪队列，不调用 sleep/sched.

# init 进程
init（pid 1）：第一个被调度的内核线程，parent 指向 swapper.
后续 kthread：自动以 init 为 parent（为将来 wait/reap 预留）.
linux 中，在系统刚刚启动时，init 进程（PID=1）是由内核的 0 号进程（idle）通过 kernel_thread 创建的。

# Linux 里概念
- scheduler（调度器）
    内核代码：schedule()、CFS、runqueue 等，没有 pid
- pid 0（swapper / idle）
    每个 CPU 上的 idle 内核线程，没任务可跑时 CPU 跑它
- pid 1（init/systemd）
    第一个用户态进程，不是调度器