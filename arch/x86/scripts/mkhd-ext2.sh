#!/bin/sh
# 生成 ext2 镜像（供内核挂到 /mnt）。
#
# 用法:
#   mkhd-ext2.sh <image> <size_mb> [src[:dst_name] ...]
#
# 额外参数写入根目录：
#   path/to/foo.bin        → /foo.bin（basename）
#   path/to/foo.bin:test   → /test
#
# 固定样例：hello.txt、dir/a.txt
set -e

usage() {
	echo "usage: $0 <image> <size_mb> [src[:dst_name] ...]" >&2
	exit 1
}

[ "$#" -ge 2 ] || usage
IMG=$1
SIZE_MB=$2
shift 2

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT
LIST=$TMPDIR/files	# 每行: src<TAB>dst

: >"$LIST"
for spec in "$@"; do
	case "$spec" in
	*:*)
		src=${spec%%:*}
		dst=${spec#*:}
		;;
	*)
		src=$spec
		dst=$(basename "$spec")
		;;
	esac
	if [ -z "$src" ] || [ -z "$dst" ]; then
		echo "$0: bad file spec '$spec'" >&2
		exit 1
	fi
	if [ ! -f "$src" ]; then
		echo "$0: missing file '$src'" >&2
		exit 1
	fi
	case "$dst" in
	*/*)
		echo "$0: dst must be a single name ('$dst')" >&2
		exit 1
		;;
	esac
	if cut -f2 "$LIST" 2>/dev/null | grep -qx "$dst"; then
		echo "$0: duplicate dst '$dst'" >&2
		exit 1
	fi
	printf '%s\t%s\n' "$src" "$dst" >>"$LIST"
done

dd if=/dev/zero of="$IMG" bs=1M count="$SIZE_MB" status=none
mkfs.ext2 -F -q -b 1024 -I 128 "$IMG"

echo 'Hello from ext2 on /mnt!' >"$TMPDIR/hello.txt"
echo 'file in dir' >"$TMPDIR/a.txt"

CMD=$TMPDIR/cmds
{
	echo "write $TMPDIR/hello.txt hello.txt"
	echo "mkdir dir"
	echo "cd dir"
	echo "write $TMPDIR/a.txt a.txt"
	echo "cd /"
	while IFS='	' read -r src dst; do
		[ -n "$src" ] || continue
		echo "write $src $dst"
	done <"$LIST"
	echo "quit"
} >"$CMD"

debugfs -w -f "$CMD" "$IMG" >/dev/null

extra=$(cut -f2 "$LIST" | tr '\n' ' ' | sed 's/[[:space:]]*$//')
if [ -n "$extra" ]; then
	echo "created $IMG (${SIZE_MB} MiB, ext2 + hello.txt, dir/a.txt, $extra)"
else
	echo "created $IMG (${SIZE_MB} MiB, ext2 + hello.txt, dir/a.txt)"
fi
