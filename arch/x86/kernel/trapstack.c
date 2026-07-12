#include "types.h"
#include "memlayout.h"

/* 内核主栈（main/普通内核代码）
 * scheduler 跑在 kernel_stack 上
 * 不属于任何 struct proc
*/
char kernel_stack[KSTACKSIZE] __attribute__((aligned(16)));

/* 中断/异常栈：trap 入口切换到此，避免与主栈嵌套冲突 */
char interrupt_stack[KSTACKSIZE] __attribute__((aligned(16)));
