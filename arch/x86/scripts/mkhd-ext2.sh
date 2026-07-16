#!/bin/sh
# 生成带样例文件的 ext2 镜像（供内核挂到 /mnt）
# 用法: mkhd-ext2.sh <image> [size_mb] [test.bin]
set -e
IMG="${1:?usage: mkhd-ext2.sh <image> [size_mb] [test.bin]}"
SIZE_MB="${2:-8}"
TEST_BIN="${3:-}"

if [ -z "$TEST_BIN" ] || [ ! -f "$TEST_BIN" ]; then
	echo "mkhd-ext2.sh: need existing test binary (got '${TEST_BIN:-}')" >&2
	echo "usage: mkhd-ext2.sh <image> [size_mb] <test.bin>" >&2
	exit 1
fi

dd if=/dev/zero of="$IMG" bs=1M count="$SIZE_MB" status=none
mkfs.ext2 -F -q -b 1024 -I 128 "$IMG"

TMPDIR=$(mktemp -d)
# 脚本无论怎样结束，退出前都删掉临时目录
trap 'rm -rf "$TMPDIR"' EXIT
echo 'Hello from ext2 on /mnt!' > "$TMPDIR/hello.txt"
echo 'file in dir' > "$TMPDIR/a.txt"

{
	echo "write $TMPDIR/hello.txt hello.txt"
	echo "mkdir dir"
	echo "cd dir"
	echo "write $TMPDIR/a.txt a.txt"
	echo "cd /"
	# 根目录可执行文件 /test（flat binary）
	echo "write $TEST_BIN test"
	echo "quit"
} | debugfs -w -f - "$IMG" >/dev/null

echo "created $IMG (${SIZE_MB} MiB, ext2 + hello.txt, dir/a.txt, test)"
