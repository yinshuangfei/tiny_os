/*
 * 用户态 init（pid 1，由 kernel_execve 加载）：
 *   - 常驻，不 exit
 *   - fork 子进程干活
 *   - waitpid 收僵尸
 * 孤儿进程由内核 reparent 到本进程。
 * 链接地址须与 memlayout.h 中 USERBASE 一致。
 */
#include "user.h"

/*
 * 从 waitpid 返回的 status 取出子进程 exit(n) 的退出码。
 * 完整 Linux 语义下 status 是编码后的状态字，需 WIFEXITED 等配合解析；
 * 本系统 waitpid 直接 copyout exit 时的 status，故取低 8 位即可。
 */
#define WEXITSTATUS(s)	((s) & 0xff)

/* 类似 getty：fork + execve 拉起 shell */
static void spawn_shell(void)
{
	int pid = fork();

	if (pid == 0) {
		execve("sh", 0, 0);
		printf("init: exec sh failed\n");
		exit(1);
	}
	if (pid < 0)
		printf("init: fork failed\n");
}

int main(void)
{
	int status;
	int pid;

	spawn_shell();

	for (;;) {
		pid = waitpid(-1, &status, 0);
		if (pid < 0) {
			/* 当前无子进程可收，稍等再拉起，避免空转打满进程表 */
			sleep(1);
			spawn_shell();
			continue;
		}
		printf("init: reaped pid=%d status=%d\n",
		       pid, WEXITSTATUS(status));
		/* 收尸后继续 spawn，保持至少有一个子进程 */
		spawn_shell();
	}
}
