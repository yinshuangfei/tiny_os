# esp
sst.ESP0 和 trapframe.oesp, trapframe.esp 什么关系?
|字段	|是什么	|属于哪条栈|
|:---|:---|:---|
|TSS.esp0|进程内核栈顶 p->kstack + 4096|内核栈（切换目标）|
|trapframe.esp|陷入瞬间的用户栈指针|用户栈（硬件保存、iret 恢复）|
|trapframe.oesp|pushal 执行前那一刻的内核 ESP|内核栈（软件保存，给 popal 用）|


# 切换模型
模型 B（你提的）：syscall 先在 kstack，仅嵌套中断用 interrupt_stack
  用户 int 0x80
    → kstack：trapframe + syscall_handler（同栈，在 trapframe 下方）
    → 若 timer 打断：IRQSTACK_SWITCH → interrupt_stack → 返回 kstack
    → syscall 结束：popal + iret（仍在 kstack）
    → 若 sleep：swtch 保存 kstack 上下文（更自然）