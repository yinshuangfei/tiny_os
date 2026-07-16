#!/bin/sh
# 生成带样例文件的 ext2 镜像（供内核挂到 /mnt）
set -e
IMG="${1:?usage: mkhd-ext2.sh <image> [size_mb]}"
SIZE_MB="${2:-8}"

# 字节 1080..1081 = superblock.s_magic (0xEF53 LE)
if [ -f "$IMG" ] && dd if="$IMG" bs=1 skip=1080 count=2 2>/dev/null | \
	od -An -tx1 | grep -q '53 *ef'; then
	echo "$IMG: keep existing ext2 image"
	exit 0
fi

dd if=/dev/zero of="$IMG" bs=1M count="$SIZE_MB" status=none
mkfs.ext2 -F -q -b 1024 -I 128 "$IMG"

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT    # 脚本无论怎样结束，退出前都删掉临时目录
echo 'Hello from ext2 on /mnt!' > "$TMPDIR/hello.txt"
echo 'file in dir' > "$TMPDIR/a.txt"

debugfs -w -f - "$IMG" <<EOF
write $TMPDIR/hello.txt hello.txt
mkdir dir
cd dir
write $TMPDIR/a.txt a.txt
quit
EOF

echo "created $IMG (${SIZE_MB} MiB, ext2 + hello.txt, dir/a.txt)"
