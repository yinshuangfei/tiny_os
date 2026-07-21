.code32

.set TF_EIP,     40
.set TF_CS,      44
.set TF_EFLAGS,  48
.set TF_ESP,     52
.set TF_SS,      56

.set SEG_UCODE_RPL3, 0x1b
.set SEG_UDATA_RPL3, 0x23

.extern cpu_user_pgdir
.extern lapic
.extern set_user_pgdir

/*
 * void user_enter(struct trapframe *tf, pagetable_t pgdir);
 * 注意：set_user_pgdir → cpu_id() 会破坏 eax/ecx/edx，须在 call 后重载 tf。
 */
.globl user_enter
user_enter:
	movl 4(%esp), %eax	/* tf */
	movl 8(%esp), %ebx	/* pgdir */

	/* 设置用户页表 */
	pushl %ebx
	call set_user_pgdir
	addl $4, %esp
	movl 4(%esp), %eax	/* 重载 tf（set_user_pgdir 已破坏 eax） */
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
