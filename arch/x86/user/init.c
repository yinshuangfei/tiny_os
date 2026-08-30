/*
 * 用户态 init（pid 1，由 kernel_execve 加载）：
 *   - 常驻，不 exit
 *   - fork 子进程干活
 *   - waitpid 收僵尸
 * 孤儿进程由内核 reparent 到本进程。
 * 链接地址须与 memlayout.h 中 USERLOAD 一致。
 */
#include "user.h"

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
			spawn_shell();
			continue;
		}
		if (WIFEXITED(status))
			printf("init: reaped pid=%d exit=%d\n",
			       pid, WEXITSTATUS(status));
		else if (WIFSIGNALED(status))
			printf("init: reaped pid=%d signal=%d\n",
			       pid, WTERMSIG(status));
		else
			printf("init: reaped pid=%d status=0x%x\n",
			       pid, status);
		/* 收尸后继续 spawn，保持至少有一个子进程 */
		spawn_shell();
	}
}
