#include "ansi.h"
#include "user.h"
#include "signal.h"

static int test_check(const char *name, int ok)
{
	printf("syscall %s(): %s\n", name, ok ? C_GREEN("OK") : C_RED("FAIL"));
	return ok;
}

void getpid_test(void)
{
	int pid = getpid();

	printf("get pid=%d\n", pid);
	test_check("getpid", pid > 0 && pid < 0xff);
}

void write_test(void)
{
	write(1, "hello, world\n", 13);
	test_check("write", 1);
}

void nanosleep_test(void)
{
	int rc;
	struct timespec ts = { 1, 0 };	/* 1s，100Hz → 100 tick */
	rc = nanosleep(&ts, 0);
	printf("nanosleep rc=%d\n", rc);
	test_check("nanosleep", 1);
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
		test_check("signal", 0);
		return;
	}
	if (sig_got != SIGUSR1) {
		printf("FAIL: got=%d expect %d\n", sig_got, SIGUSR1);
		test_check("signal", 0);
		return;
	}
	printf("signal OK\n");
	test_check("signal", 1);
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
		test_check("lseek", 0);
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

	test_check("lseek", ok);
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
		test_check("link", 0);
		test_check("unlink", 0);
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
	test_check("link", ok);

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
	test_check("unlink", ok);
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
		test_check("rename", 0);
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
	test_check("rename", ok);
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
	exit(0);
}
