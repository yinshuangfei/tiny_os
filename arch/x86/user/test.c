#include "ansi.h"
#include "user.h"
#include "signal.h"

static int syscall_pr_result(const char *name, int ok)
{
	printf("* syscall %s(): %s\n", name, ok ? C_GREEN("OK") : C_RED("FAIL"));
	return ok;
}

static int lib_pr_result(const char *name, int ok)
{
	printf("* lib %s(): %s\n", name, ok ? C_GREEN("OK") : C_RED("FAIL"));
	return ok;
}

void getpid_test(void)
{
	int pid = getpid();
	syscall_pr_result("getpid", pid > 0 && pid < 0xff);
}

void write_test(void)
{
	char *buf = "hello, world\n";
	int rc = write(1, buf, strlen(buf));
	syscall_pr_result("write", rc == strlen(buf));
}

void nanosleep_test(void)
{
	int rc;
	struct timespec ts = { 0, 500000000 };	/* 1s，100Hz → 100 tick */
	rc = nanosleep(&ts, 0);
	syscall_pr_result("nanosleep", rc == 0);
}

static volatile int sig_got;

static void on_usr1(int sig)
{
	sig_got = sig;
	printf("handler: got signal %d\n", sig);
}

/* 注册 SIGUSR1 handler，向自身 kill，验证 handler 与 sigreturn */
void signal_test(void)
{
	sighandler_t old;

	old = signal(SIGUSR1, on_usr1);
	(void)old;

	sig_got = 0;
	if (kill(getpid(), SIGUSR1) < 0) {
		printf("kill self failed\n");
		syscall_pr_result("signal", 0);
		return;
	}
	if (sig_got != SIGUSR1) {
		printf("FAIL: got=%d expect %d\n", sig_got, SIGUSR1);
		syscall_pr_result("signal", 0);
		return;
	}
	syscall_pr_result("signal", 1);
}

/*
 * lseek：对 /hello 测 SEEK_SET / SEEK_CUR / SEEK_END，并确认管道不可定位。
 * 文件内容：Hello from Tiny-OS ramfs!\n
 */
void lseek_test(void)
{
	int fd, n, off, ok;
	char buf[8];
	int p[2];

	ok = 1;
	fd = open("/hello", O_RDONLY);
	if (fd < 0) {
		printf("lseek: open /hello failed\n");
		syscall_pr_result("lseek", 0);
		return;
	}

	/* SEEK_SET → 偏移 6，读 "from" */
	off = lseek(fd, 6, SEEK_SET);
	if (off != 6) {
		printf("lseek SEEK_SET: off=%d expect 6\n", off);
		ok = 0;
	}
	n = read(fd, buf, 4);
	if (n != 4 || buf[0] != 'f' || buf[1] != 'r' ||
	    buf[2] != 'o' || buf[3] != 'm') {
		printf("lseek SEEK_SET read failed n=%d\n", n);
		ok = 0;
	}

	/* SEEK_CUR：当前位置 10，回退 4 → 6 */
	off = lseek(fd, -4, SEEK_CUR);
	if (off != 6) {
		printf("lseek SEEK_CUR: off=%d expect 6\n", off);
		ok = 0;
	}

	/* SEEK_END：到末尾再退 1，应读到 '\n' */
	off = lseek(fd, 0, SEEK_END);
	if (off <= 0) {
		printf("lseek SEEK_END: off=%d\n", off);
		ok = 0;
	}
	off = lseek(fd, -1, SEEK_END);
	if (off < 0) {
		printf("lseek SEEK_END-1 failed\n");
		ok = 0;
	}
	n = read(fd, buf, 1);
	if (n != 1 || buf[0] != '\n') {
		printf("lseek SEEK_END read failed n=%d c=%d\n",
		       n, n == 1 ? (int)buf[0] : -1);
		ok = 0;
	}

	/* 负偏移非法 */
	if (lseek(fd, -1, SEEK_SET) != -1) {
		printf("lseek negative SEEK_SET should fail\n");
		ok = 0;
	}
	close(fd);

	/* 管道不可 lseek（类 ESPIPE） */
	if (pipe(p) < 0) {
		printf("lseek: pipe failed\n");
		ok = 0;
	} else {
		if (lseek(p[0], 0, SEEK_SET) != -1 ||
		    lseek(p[1], 0, SEEK_SET) != -1) {
			printf("lseek on pipe should fail\n");
			ok = 0;
		}
		close(p[0]);
		close(p[1]);
	}

	syscall_pr_result("lseek", ok);
}

/* 写小文件；成功返回 0 */
static int write_file(const char *path, const char *s)
{
	int fd, n, len;

	fd = open(path, O_CREATE | O_WRONLY);
	if (fd < 0)
		return -1;
	len = 0;
	while (s[len])
		len++;
	n = write(fd, s, len);
	close(fd);
	return n == len ? 0 : -1;
}

/* 读至多 max-1 字节并加 '\0'；成功返回字节数 */
static int read_file(const char *path, char *buf, int max)
{
	int fd, n;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	n = read(fd, buf, max - 1);
	close(fd);
	if (n < 0)
		return -1;
	buf[n] = '\0';
	return n;
}

static int streq(const char *a, const char *b)
{
	return strcmp(a, b) == 0;
}

/*
 * link / unlink：硬链接共享内容；删一条链路后另一条仍可读。
 */
void link_unlink_test(void)
{
	char buf[32];
	int ok = 1;

	unlink("/t_a");
	unlink("/t_b");

	if (write_file("/t_a", "linkdata") < 0) {
		printf("link: create /t_a failed\n");
		syscall_pr_result("link", 0);
		syscall_pr_result("unlink", 0);
		return;
	}

	if (link("/t_a", "/t_b") < 0) {
		printf("link: link /t_a -> /t_b failed\n");
		ok = 0;
	}
	if (read_file("/t_b", buf, sizeof(buf)) < 0 || !streq(buf, "linkdata")) {
		printf("link: read /t_b failed\n");
		ok = 0;
	}
	/* 不可硬链接目录 */
	if (link("/", "/t_rootlink") != -1) {
		printf("link: linking dir should fail\n");
		ok = 0;
		unlink("/t_rootlink");
	}
	syscall_pr_result("link", ok);

	ok = 1;
	if (unlink("/t_a") < 0) {
		printf("unlink: /t_a failed\n");
		ok = 0;
	}
	/* 原名已删，硬链接仍在 */
	if (open("/t_a", O_RDONLY) >= 0) {
		printf("unlink: /t_a still exists\n");
		ok = 0;
	}
	if (read_file("/t_b", buf, sizeof(buf)) < 0 || !streq(buf, "linkdata")) {
		printf("unlink: /t_b should still work\n");
		ok = 0;
	}
	if (unlink("/t_b") < 0) {
		printf("unlink: /t_b failed\n");
		ok = 0;
	}
	/* 不可 unlink 目录 */
	if (unlink("/") != -1 || unlink("/dev") != -1) {
		printf("unlink: dir should fail\n");
		ok = 0;
	}
	syscall_pr_result("unlink", ok);
}

/*
 * rename：同目录改名、跨目录移动；目标已存在时可覆盖普通文件。
 */
void rename_test(void)
{
	char buf[32];
	int ok = 1;
	int fd;

	unlink("/t_r1");
	unlink("/t_r2");
	unlink("/t_sub/x");
	rmdir("/t_sub");

	if (write_file("/t_r1", "renamed") < 0) {
		printf("rename: create failed\n");
		syscall_pr_result("rename", 0);
		return;
	}

	/* 同目录改名 */
	if (rename("/t_r1", "/t_r2") < 0) {
		printf("rename: /t_r1 -> /t_r2 failed\n");
		ok = 0;
	}
	if (open("/t_r1", O_RDONLY) >= 0) {
		printf("rename: old path still exists\n");
		ok = 0;
	}
	if (read_file("/t_r2", buf, sizeof(buf)) < 0 || !streq(buf, "renamed")) {
		printf("rename: new path content wrong\n");
		ok = 0;
	}

	/* 跨目录移动 */
	if (mkdir("/t_sub", 0) < 0) {
		printf("rename: mkdir /t_sub failed\n");
		ok = 0;
	} else if (rename("/t_r2", "/t_sub/x") < 0) {
		printf("rename: move into /t_sub failed\n");
		ok = 0;
	} else if (read_file("/t_sub/x", buf, sizeof(buf)) < 0 ||
		   !streq(buf, "renamed")) {
		printf("rename: /t_sub/x content wrong\n");
		ok = 0;
	}

	/* 覆盖已存在普通文件 */
	if (write_file("/t_r1", "old") < 0 || write_file("/t_r2", "new") < 0) {
		printf("rename: setup overwrite failed\n");
		ok = 0;
	} else if (rename("/t_r2", "/t_r1") < 0) {
		printf("rename: overwrite failed\n");
		ok = 0;
	} else if (read_file("/t_r1", buf, sizeof(buf)) < 0 ||
		   !streq(buf, "new")) {
		printf("rename: overwrite content wrong\n");
		ok = 0;
	}

	/* ext2 未实现：对 /mnt 应失败 */
	fd = open("/mnt/hello.txt", O_RDONLY);
	if (fd >= 0) {
		close(fd);
		if (rename("/mnt/hello.txt", "/mnt/hello.bak") != -1) {
			printf("rename: ext2 should fail\n");
			ok = 0;
			rename("/mnt/hello.bak", "/mnt/hello.txt");
		}
	}

	unlink("/t_r1");
	unlink("/t_r2");
	unlink("/t_sub/x");
	rmdir("/t_sub");
	syscall_pr_result("rename", ok);
}

/*
 * wait：无子进程失败；fork 子进程 exit 后用 WIFEXITED / WEXITSTATUS 解析。
 */
void wait_test(void)
{
	int pid, wpid, status, ok;

	ok = 1;

	/* 当前无子进程时应失败 */
	if (wait(&status) != -1) {
		printf("wait: no child should fail\n");
		ok = 0;
	}

	pid = fork();
	if (pid < 0) {
		printf("wait: fork failed\n");
		syscall_pr_result("wait", 0);
		return;
	}
	if (pid == 0)
		exit(42);

	status = -1;
	wpid = wait(&status);
	if (wpid != pid) {
		printf("wait: wpid=%d expect %d\n", wpid, pid);
		ok = 0;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 42) {
		printf("wait: status=0x%x exited=%d code=%d\n",
		       status, WIFEXITED(status), WEXITSTATUS(status));
		ok = 0;
	}
	if (WIFSIGNALED(status)) {
		printf("wait: unexpectedly signaled\n");
		ok = 0;
	}

	/* 子进程已收完，再次 wait 应失败 */
	if (wait(&status) != -1) {
		printf("wait: second wait should fail\n");
		ok = 0;
	}

	/* status == NULL 也可等待 */
	pid = fork();
	if (pid < 0) {
		printf("wait: fork2 failed\n");
		ok = 0;
	} else if (pid == 0) {
		exit(0);
	} else if (wait(0) != pid) {
		printf("wait: wait(NULL) failed\n");
		ok = 0;
	}

	lib_pr_result("wait", ok);
}

/*
 * fork COW：父子写同一全局缓冲应互不影响；且 fork 后空闲页不应骤降（共享而非全拷）。
 */
static unsigned int mem_free_kb(void)
{
	char buf[256];
	char line[64];
	int fd, n, i, li;
	unsigned int free_kb;

	fd = open("/proc/meminfo", O_RDONLY);
	if (fd < 0)
		return 0;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return 0;
	buf[n] = '\0';

	free_kb = 0;
	li = 0;
	for (i = 0; i <= n; i++) {
		char c = (i < n) ? buf[i] : '\n';

		if (c == '\n' || c == '\0') {
			line[li] = '\0';
			if (li > 8 && line[0] == 'M' && line[3] == 'F') {
				/* MemFree: */
				const char *p = line;
				while (*p && (*p < '0' || *p > '9'))
					p++;
				while (*p >= '0' && *p <= '9') {
					free_kb = free_kb * 10 + (*p - '0');
					p++;
				}
				return free_kb;
			}
			li = 0;
		} else if (li + 1 < (int)sizeof(line)) {
			line[li++] = c;
		}
	}
	return free_kb;
}

static char cow_slot[64];

void cow_fork_test(void)
{
	int pid, status, ok;
	unsigned int free0, free1;

	ok = 1;
	strcpy(cow_slot, "parent-data");

	free0 = mem_free_kb();
	pid = fork();
	if (pid < 0) {
		printf("cow: fork failed\n");
		syscall_pr_result("fork-cow", 0);
		return;
	}
	if (pid == 0) {
		/* 子进程写入应触发 COW，不影响父进程 */
		cow_slot[0] = 'C';
		if (cow_slot[0] != 'C' || cow_slot[1] != 'a')
			exit(1);
		exit(0);
	}

	free1 = mem_free_kb();
	/*
	 * COW：fork 后主要只多页表等少量页；全量拷贝会掉很多（用户映像数页）。
	 * 允许消耗 < 64 KiB（页表等）；过大则不像 COW。
	 */
	if (free0 > free1 && (free0 - free1) > 64) {
		printf("cow: freeram dropped %u KiB (expect COW share)\n",
		       free0 - free1);
		ok = 0;
	}

	/* 父进程也写，验证隔离 */
	cow_slot[0] = 'P';
	if (wait(&status) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) {
		printf("cow: child status bad\n");
		ok = 0;
	}
	if (cow_slot[0] != 'P' || strcmp(cow_slot + 1, "arent-data") != 0) {
		printf("cow: parent buf corrupted: %s\n", cow_slot);
		ok = 0;
	}

	syscall_pr_result("fork-cow", ok);
}

/*
 * brk / sbrk：扩展堆、写入、再收缩；非法 brk 应保持原断点。
 */
void brk_test(void)
{
	char *p, *q, *old;
	int ok = 1;
	int i;

	old = (char *)brk((void *)0);
	if (old == 0 || old == (char *)-1) {
		printf("brk: query failed\n");
		syscall_pr_result("brk", 0);
		return;
	}

	p = sbrk(4096);
	if (p == (char *)-1 || p != old) {
		printf("brk: sbrk grow failed\n");
		ok = 0;
	} else {
		for (i = 0; i < 4096; i++)
			p[i] = (char)(i & 0xff);
		for (i = 0; i < 4096; i++) {
			if (p[i] != (char)(i & 0xff)) {
				printf("brk: heap write/read mismatch\n");
				ok = 0;
				break;
			}
		}
	}

	q = (char *)brk(old);
	if (q != old) {
		printf("brk: shrink to old failed ret=%p expect=%p\n", q, old);
		ok = 0;
	}

	/* 非法：低于起始断点 → 仍返回当前断点 */
	q = (char *)brk((void *)0x1000);
	if (q != old) {
		printf("brk: bad addr should return current\n");
		ok = 0;
	}

	syscall_pr_result("brk", ok);
	lib_pr_result("sbrk", ok);
}

/*
 * 匿名 mmap / munmap：映射、读写、卸映射；非法 flags 应失败。
 */
void mmap_test(void)
{
	char *p;
	int ok = 1;
	int i;

	p = mmap(0, 4096, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED || p == 0) {
		printf("mmap: anonymous map failed\n");
		syscall_pr_result("mmap", 0);
		lib_pr_result("munmap", 0);
		return;
	}

	for (i = 0; i < 4096; i++)
		p[i] = (char)(i & 0xff);
	for (i = 0; i < 4096; i++) {
		if (p[i] != (char)(i & 0xff)) {
			printf("mmap: write/read mismatch\n");
			ok = 0;
			break;
		}
	}

	if (munmap(p, 4096) < 0) {
		printf("mmap: munmap failed\n");
		ok = 0;
	}

	/* 非匿名应失败 */
	p = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, -1, 0);
	if (p != MAP_FAILED) {
		printf("mmap: non-anonymous should fail\n");
		ok = 0;
		munmap(p, 4096);
	}

	lib_pr_result("mmap", ok);
	syscall_pr_result("munmap", ok);
}

/*
 * FPU/SSE：x87 算术、%f 打印，以及 fork 后 XMM 寄存器隔离（lazy FXSAVE）。
 */
void fpu_test(void)
{
	float a, b, c, got;
	int pid, status, ok = 1;
	int i;
	struct timespec ts;
	static const float child_val = 2.5f;
	static const float parent_val = 7.25f;

	a = 1.5f;
	b = 2.25f;
	c = a + b * 2.0f;	/* 6.0 */
	if (c < 5.9f || c > 6.1f) {
		printf("fpu: arith fail c=%f\n", (double)c);
		ok = 0;
	}

	pid = fork();
	if (pid < 0) {
		printf("fpu: fork failed\n");
		ok = 0;
	} else if (pid == 0) {
		__asm__ volatile("movss %0, %%xmm0" : : "m"(child_val) : "xmm0");
		ts.tv_sec = 0;
		ts.tv_nsec = 20000000;	/* 20ms，给父进程抢占机会 */
		for (i = 0; i < 5; i++)
			nanosleep(&ts, 0);
		__asm__ volatile("movss %%xmm0, %0" : "=m"(got) : : "xmm0");
		exit((got > 2.4f && got < 2.6f) ? 0 : 1);
	} else {
		ts.tv_sec = 0;
		ts.tv_nsec = 10000000;
		nanosleep(&ts, 0);
		__asm__ volatile("movss %0, %%xmm0" : : "m"(parent_val) : "xmm0");
		for (i = 0; i < 5; i++)
			nanosleep(&ts, 0);
		if (waitpid(pid, &status, 0) != pid ||
		    !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			printf("fpu: child xmm corrupted status=0x%x\n", status);
			ok = 0;
		}
		__asm__ volatile("movss %%xmm0, %0" : "=m"(got) : : "xmm0");
		if (got < 7.2f || got > 7.3f) {
			printf("fpu: parent xmm corrupted got=%f\n", (double)got);
			ok = 0;
		}
	}

	syscall_pr_result("fpu", ok);
}

/*
 * 按需分页：大块 mmap/brk 不立即吃物理页；触碰后才分配；未映射页读为 0。
 */
void demand_paging_test(void)
{
	char *p, *old;
	unsigned int free0, free1, free2;
	int ok = 1;
	const unsigned int nbytes = 64 * 4096;

	free0 = mem_free_kb();
	p = mmap(0, nbytes, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED || p == 0) {
		printf("demand: mmap failed\n");
		syscall_pr_result("demand-page", 0);
		return;
	}

	free1 = mem_free_kb();
	/* 懒分配：64 页不应立刻消耗 ~256 KiB */
	if (free0 > free1 && (free0 - free1) > 32) {
		printf("demand: mmap ate %u KiB (expect lazy)\n",
		       free0 - free1);
		ok = 0;
	}

	/* 未触碰页读应为 0 */
	if (p[0] != 0 || p[4096] != 0) {
		printf("demand: untouched page not zero\n");
		ok = 0;
	}

	p[0] = 'A';
	p[8192] = 'B';
	free2 = mem_free_kb();
	/* 触碰约 3 页（0、1、2），消耗应远小于 64 页 */
	if (free1 > free2 && (free1 - free2) > 48) {
		printf("demand: touch ate %u KiB (too many)\n",
		       free1 - free2);
		ok = 0;
	}
	if (p[0] != 'A' || p[8192] != 'B') {
		printf("demand: write/read mismatch\n");
		ok = 0;
	}

	if (munmap(p, nbytes) < 0) {
		printf("demand: munmap failed\n");
		ok = 0;
	}

	/* brk 同样懒扩展 */
	old = (char *)brk((void *)0);
	free0 = mem_free_kb();
	if (brk(old + nbytes) != old + nbytes) {
		printf("demand: brk grow failed\n");
		ok = 0;
	} else {
		free1 = mem_free_kb();
		if (free0 > free1 && (free0 - free1) > 32) {
			printf("demand: brk ate %u KiB eagerly\n",
			       free0 - free1);
			ok = 0;
		}
		old[0] = 'Z';
		if (old[0] != 'Z') {
			printf("demand: brk fault-in failed\n");
			ok = 0;
		}
		brk(old);
	}

	syscall_pr_result("demand-page", ok);
}

/**
 * 可以只使用
 * int main(int argc, char *argv[])
 */
int main(int argc, char *argv[], char *envp[])
{
	int i;

	for (i = 0; i < argc; i++)
		printf("argv[%d] = %s\n", i, argv[i]);

	if (envp) {
		for (i = 0; envp[i]; i++)
			printf("envp[%d] = %s\n", i, envp[i]);
	}

	write_test();
	getpid_test();
	nanosleep_test();
	signal_test();
	lseek_test();
	link_unlink_test();
	rename_test();
	wait_test();
	cow_fork_test();
	brk_test();
	mmap_test();
	fpu_test();
	demand_paging_test();
	exit(0);
}
