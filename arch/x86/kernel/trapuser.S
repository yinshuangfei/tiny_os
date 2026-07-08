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
 */
.globl user_enter
user_enter:
	movl 4(%esp), %eax	/* tf */
	movl 8(%esp), %ebx	/* pgdir */

	movl %ebx, current_user_pgdir
	movl %ebx, %cr3

	pushl TF_SS(%eax)
	pushl TF_ESP(%eax)
	pushl TF_EFLAGS(%eax)
	pushl TF_CS(%eax)
	pushl TF_EIP(%eax)
	iret
