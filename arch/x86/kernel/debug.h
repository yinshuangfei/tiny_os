#ifndef __DEBUG_H__
#define __DEBUG_H__

#include "types.h"

void pr_buf(uchar *buf, uint64 size);
void dump_pagetable(void);

#endif /** __DEBUG_H__ */