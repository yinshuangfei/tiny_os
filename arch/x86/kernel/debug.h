#ifndef __DEBUG_H__
#define __DEBUG_H__

#include "types.h"

void pr_buf(uchar *buf, uint64 size);

/** 打印页表 */
void dump_pagetable(void);

/** 打印进程表 */
void dump_proc_table(void);

#endif /** __DEBUG_H__ */