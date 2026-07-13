#ifndef __TRAP_H__
#define __TRAP_H__

#include "types.h"

/*
 * trap.S 压栈布局（esp 指向 edi）：
 * pushal → ds → err → eip → cs → eflags [→ esp → ss]
 * 必须与 trap.S 中 TRAP_FRAME_ENTER 及硬件压栈顺序一致。
 */
struct trapframe {
	/* pushal 压栈的部分 */
	uint32 edi;	// esp+ 0  ← popal (最后压入，最先弹出)
	uint32 esi;	// esp+ 4
	uint32 ebp;	// esp+ 8
	uint32 oesp;	// esp+12  old esp, 压栈之前的 ESP（不是当前 ESP）
	uint32 ebx;	// esp+16
	uint32 edx;	// esp+20
	uint32 ecx;	// esp+24
	uint32 eax;	// esp+28  ← pushal 最先压入 (最后弹出)

	uint32 ds;	// esp+32 数据段选择子
	uint32 err;	// esp+36 错误码

	/*
	 * ring3 -> ring0 硬件压栈的部分，eip, cs, eflags, esp, ss
	 * ring0 -> ring3 硬件压栈的部分，eip, cs, eflags, esp, ss

	 * ring0 -> ring0 硬件压栈的部分，eip, cs, eflags
	 */
	uint32 eip;	// esp+40 指令指针
	uint32 cs;	// esp+44 代码段选择子
	uint32 eflags;	// esp+48 标志寄存器
	// ring3 硬件压栈的用户 esp；内核态 trap 时无效
	uint32 esp;	// esp+52 栈顶指针（陷入瞬间的用户栈指针）
	// ring3 硬件压栈的用户 ss；内核态 trap 时无效
	uint32 ss;	// esp+56 栈段选择子 (最先压入，最后弹出)

}; /* 60 bytes */

#endif
