#ifndef __UTILS_H__
#define __UTILS_H__

#include "types.h"

/** bytes_to_human 输出缓冲建议长度 */
#define HUMAN_SIZE_MAX  24

/** 字节数 → "129 MiB" 等，返回写入长度，失败 -1 */
int bytes_to_human(uint64 bytes, char *buf, uint buflen);

/** "129M" / "4G" / "512" → 字节数，成功 0，失败 -1 */
int human_to_bytes(const char *s, uint64 *bytes);

#endif
