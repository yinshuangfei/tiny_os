#include "defs.h"
#include "types.h"

void pr_buf(uchar *buf, uint64 size)
{
	int ident = 16;
	printf("[%d] ", 0);
	for (uint64 i = 0; i < size; i++) {
		if (i != 0 && i % ident == 0) {
			printf("\n[%d] ", i);
		}
		printf("%x ", buf[i]);
	}
	printf("\n");
}

void dump_pagetable(void)
{
	// printf("mmap: va:0x%lx, pa:0x%lx, size:%ld\n", va, pa, size);
}

void dump_proc_table(void)
{
	struct proc *p;

	for (int i = 0; i < NPROC; i++) {
		p = &proc_table[i];
		if (p->state == UNUSED)
			continue;

		printf("pid:%d, ppid:%d, %s, %s\n",
			p->pid, p->parent->pid, p->name, procstate_str[p->state]);
	}
}
