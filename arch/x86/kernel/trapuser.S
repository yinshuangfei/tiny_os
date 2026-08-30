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

/*
 * void user_enter(struct trapframe *tf, pagetable_t pgdir_phys);
 * 调用方已先通过 set_user_pgdir() 记录当前 CPU 的用户 CR3 物理地址，
 * 这里直接装入 CR3 并 iret 到 ring3，避免对物理地址再做一次 V2P。
 */
.globl user_enter
user_enter:
	movl 4(%esp), %eax	/* tf */
	movl 8(%esp), %ebx	/* pgdir_phys */
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
