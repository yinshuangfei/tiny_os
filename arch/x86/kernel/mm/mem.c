/*
 * 物理内存大小探测：
 *   1. QEMU fw_cfg（-kernel 路径，读 -m 参数）
 *   2. boot_info（MBR 路径，setup 通过 BIOS E820 写入）
 *
 * 结果写入 physmem_top（页对齐）。仅受 32 位恒等映射上界 PHYSMEM_TOP_MAX 约束。
 */
#include "../types.h"
#include "../defs.h"
#include "memlayout.h"
#include "../x86.h"

uint physmem_top;

#define FW_CFG_PORT_SEL   0x510
#define FW_CFG_PORT_DATA  0x511
#define FW_CFG_SIGNATURE  0x0000
#define FW_CFG_RAM_SIZE   0x0003	/* 0x17 是 FW_CFG_SETUP_SIZE，勿混用 */

static inline uint8 fw_cfg_read8(void)
{
	return inb(FW_CFG_PORT_DATA);
}

static inline void fw_cfg_select(uint16 key)
{
	outw(key, FW_CFG_PORT_SEL);
}

static int fw_cfg_ram_size(uint *top)
{
	uint32 sig;
	uint64 ram;
	int i;

	fw_cfg_select(FW_CFG_SIGNATURE);
	sig = fw_cfg_read8();
	sig |= (uint32)fw_cfg_read8() << 8;
	sig |= (uint32)fw_cfg_read8() << 16;
	sig |= (uint32)fw_cfg_read8() << 24;
	if (sig != 0x554d4551)	/* 'QEMU' little-endian */
		return -1;

	fw_cfg_select(FW_CFG_RAM_SIZE);
	ram = 0;
	for (i = 0; i < 8; i++)
		ram |= (uint64)fw_cfg_read8() << (8 * i);

	if (ram == 0)
		return -1;
	/* 非 PAE：只取低 32 位可恒等映射的部分 */
	if (ram > (uint64)PHYSMEM_TOP_MAX)
		ram = PHYSMEM_TOP_MAX;

	*top = (uint)ram;
	return 0;
}

static int boot_info_ram_size(uint *top)
{
	volatile uint32 *info = (volatile uint32 *)BOOT_INFO;

	if (info[0] != BOOT_INFO_MAGIC)
		return -1;
	if (info[1] <= KERNBASE)
		return -1;
	*top = info[1];
	return 0;
}

void mem_probe(void)
{
	uint top = 0;
	char human[HUMAN_SIZE_MAX];

	if (fw_cfg_ram_size(&top) < 0)
		if (boot_info_ram_size(&top) < 0)
			panic("mem: cannot detect RAM size");

	top = PGROUNDDOWN(top);
	if (top > PHYSMEM_TOP_MAX)
		top = PHYSMEM_TOP_MAX;
	if (top <= KERNBASE) {
		printf("mem: RAM size: 0x%x, KERNBASE: 0x%x\n", top, KERNBASE);
		panic("mem: RAM too small, below the KERNBASE");
	}

	physmem_top = top;
	bytes_to_human(top, human, sizeof(human));
	printf("mem: detected RAM 0x0 - 0x%x (%s)\n", top, human);
}
