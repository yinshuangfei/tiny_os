.code32

.set TF_EIP,     40
.set TF_CS,      44
.set TF_EFLAGS,  48
.set TF_ESP,     52
.set TF_SS,      56

.set SEG_UCODE_RPL3, 0x1b
.set SEG_UDATA_RPL3, 0x23

.extern current_user_pgdir

/*
 * void user_enter(struct trapframe *tf, pagetable_t pgdir);
 * 加载用户页表并通过 iret 进入 ring 3，不返回。
 *
 * iret 只切换 CS/SS；DS/ES/FS/GS 须在进入 ring3 前手动装成用户数据段，
 * 否则 CPL=3 仍用内核 DS（DPL=0）访存会 #GP（VMware 等较严格，QEMU 有时放过）。
 */
.globl user_enter
user_enter:
	movl 4(%esp), %eax	/* tf */
	movl 8(%esp), %ebx	/* pgdir */

	/* 设置用户页表 */
	movl %ebx, current_user_pgdir
	movl %ebx, %cr3

	/* 用户数据段（RPL=3）；SS 由下面 iret 加载 */
	movw $SEG_UDATA_RPL3, %cx
	movw %cx, %ds
	movw %cx, %es
	movw %cx, %fs
	movw %cx, %gs

	pushl TF_SS(%eax)
	pushl TF_ESP(%eax)
	pushl TF_EFLAGS(%eax)
	pushl TF_CS(%eax)
	pushl TF_EIP(%eax)
	iret
