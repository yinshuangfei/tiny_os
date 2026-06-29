//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "defs.h"
#include "param.h"

uint64 sys_exec(void)
{
	char path[MAXPATH], *argv[MAXARG];
	int i;
	/**
	 * uargv: 存储[参数地址]数组的地址
	 * 	- 参数地址1: 存储参数 1 所在地址的指针
	 * 	- 参数地址2: ...
	 * 	...
	 *  */
	uint64 uargv, uarg;

	if (argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0) {
		return -1;
	}

	memset(argv, 0, sizeof(argv));

	for (i=0;; i++) {
		if (i >= NELEM(argv)) {
			printf("[ERROR] name len error\n");
			goto bad;
		}
		// 获取地址中的指针
		if (fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0) {
			printf("[ERROR] uarg error\n");
			goto bad;
		}
		if (uarg == 0) {
			argv[i] = 0;
			break;
		}
		argv[i] = kalloc();
		if (argv[i] == 0) {
			printf("[ERROR] kalloc error\n");
			goto bad;
		}
		// 读取指针地址处的参数
		if (fetchstr(uarg, argv[i], PGSIZE) < 0) {
			printf("[ERROR] fetchstr error\n");
			goto bad;
		}
	}

	int ret = exec(path, argv);

	for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
		kfree(argv[i]);

	return ret;

bad:
	for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
		kfree(argv[i]);
	return -1;
}