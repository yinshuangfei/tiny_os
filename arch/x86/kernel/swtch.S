.code32

/*
 * void swtch(struct context *old, struct context *new);
 *
 * 保存 callee-saved（eip/esp/ebx/esi/edi/ebp）到 old，从 new 恢复并 ret 到 new->eip。
 * 布局见 proc.h struct context。
 * 进入 swtch 时的栈（cdecl 调用约定）：
 高地址
    ┌──────────────┐
    │  new (arg2)  │  8(%esp)
    ├──────────────┤
    │  old (arg1)  │  4(%esp)
    ├──────────────┤
    │  ret address │  (%esp)   ← 调用 swtch 的下一条指令
    └──────────────┘
低地址
 * 栈上的内容不是 swtch 自己压的，而是 调用 swtch 的 C 代码 + call 指令 完成的。
 * 编译器会生成类似下面的汇编（cdecl，32 位）：

pushl  $c->context      ; 先压 arg2: new
pushl  $p->context      ; 再压 arg1: old
call   swtch            ; call 再压返回地址
 */
.globl swtch
swtch:
	movl 4(%esp), %eax	# eax = old，指向 struct context
	movl 8(%esp), %edx	# new

	# 保存当前上下文 → old
	movl (%esp), %ecx
	movl %ecx, 0(%eax)	# old->eip = 返回地址（swtch 调用点的下一条）
	movl %ebx, 8(%eax)
	movl %esi, 12(%eax)
	movl %edi, 16(%eax)
	movl %ebp, 20(%eax)
	movl %esp, 4(%eax)	# old->esp = 当前栈顶（指向返回地址）

	# 恢复新上下文 ← new
	movl 4(%edx), %esp	# 加载 new->esp
	movl 8(%edx), %ebx
	movl 12(%edx), %esi
	movl 16(%edx), %edi
	movl 20(%edx), %ebp
	movl 0(%edx), %ecx	# new->eip
	pushl %ecx

	# 从栈 pop 一个地址并跳转，这里先 push eip 再 ret，等价于跳到 new->eip
	# ret 到 eip，不是正常的 call 指令，而是直接跳转到 eip 地址
	ret
