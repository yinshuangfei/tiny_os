#!/usr/bin/env python3
"""
e2e：双 ext2 挂载隔离。

步骤（对齐手工复现）：
  1. umount 启动时自动挂载点
  2. mount /dev/hda /mnt
  3. mkdir /a
  4. mount /dev/hdb /a
  5. 检查 /mnt 与 /a 内容互不串味

用法（在 arch/x86 下）：
  python3 e2e_test/test_dual_mount.py
  make e2e-dual-mount
"""
from __future__ import annotations

import os
import pty
import re
import select
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]  # arch/x86
KERNEL = ROOT / "kernel.elf"
MKHD = ROOT / "scripts" / "mkhd-ext2.sh"
QEMU = os.environ.get("QEMU", "qemu-system-x86_64")
HD_SIZE_MB = os.environ.get("HD_SIZE_MB", "8")

ANSI = re.compile(br"\x1b\[[0-9;]*[A-Za-z]")


def clean(b: bytes) -> bytes:
	return ANSI.sub(b"", b)


def run(cmd: list[str], **kw) -> None:
	print("+", " ".join(cmd), flush=True)
	subprocess.check_call(cmd, **kw)


def prepare_images(work: Path) -> tuple[Path, Path]:
	"""生成两块内容不同的 ext2 盘。"""
	hda = work / "hda.img"
	hdb = work / "hdb.img"
	marker_a = work / "marker_hda"
	marker_b = work / "marker_hdb"
	marker_a.write_text("from-hda\n")
	marker_b.write_text("from-hdb\n")

	run(["sh", str(MKHD), str(hda), HD_SIZE_MB, f"{marker_a}:marker_hda"])

	# hdb：仅 marker，与 hda 明显不同
	run(["dd", "if=/dev/zero", f"of={hdb}", "bs=1M", f"count={HD_SIZE_MB}",
	     "status=none"])
	run(["mkfs.ext2", "-F", "-q", "-b", "1024", "-I", "128", str(hdb)])
	run(["debugfs", "-w", "-R", f"write {marker_b} marker_hdb", str(hdb)],
	    stdout=subprocess.DEVNULL)

	return hda, hdb


def qemu_cmd(hda: Path, hdb: Path) -> list[str]:
	return [
		QEMU, "-machine", "q35", "-m", "128", "-smp", "2",
		"-nographic", "-display", "none",
		"-device", "piix3-ide,id=ide0",
		"-drive", f"file={hda},format=raw,if=none,id=hd0,snapshot=on",
		"-device", "ide-hd,drive=hd0,bus=ide0.0,unit=0",
		"-drive", f"file={hdb},format=raw,if=none,id=hd1,snapshot=on",
		"-device", "ide-hd,drive=hd1,bus=ide0.0,unit=1",
		"-kernel", str(KERNEL),
	]


def drain(fd: int, out: bytearray, timeout: float = 0.05) -> None:
	end = time.time() + timeout
	while time.time() < end:
		r, _, _ = select.select([fd], [], [], max(0.0, end - time.time()))
		if fd not in r:
			break
		try:
			chunk = os.read(fd, 8192)
		except OSError:
			break
		if not chunk:
			break
		out.extend(chunk)
		sys.stdout.buffer.write(chunk)
		sys.stdout.buffer.flush()


def run_guest(hda: Path, hdb: Path) -> str:
	cmd = qemu_cmd(hda, hdb)
	print("+", " ".join(cmd), flush=True)

	pid, fd = pty.fork()
	if pid == 0:
		os.chdir(ROOT)
		os.execvp(cmd[0], cmd)

	out = bytearray()
	deadline = time.time() + 25
	while time.time() < deadline:
		r, _, _ = select.select([fd], [], [], 0.3)
		if fd in r:
			try:
				chunk = os.read(fd, 8192)
			except OSError:
				break
			if not chunk:
				break
			out.extend(chunk)
			sys.stdout.buffer.write(chunk)
			sys.stdout.buffer.flush()
		if b"Welcome to Tiny-OS" in clean(bytes(out)):
			break
	else:
		os.close(fd)
		try:
			os.waitpid(pid, 0)
		except Exception:
			pass
		raise SystemExit("FAIL: timeout waiting for shell")

	time.sleep(3.5)
	drain(fd, out, 0.3)

	cmds = [
		b"umount /mnt\n",
		b"umount /mnt1\n",
		b"mount /dev/hda /mnt\n",
		b"mkdir /a\n",
		b"mount /dev/hdb /a\n",
		b"ls /mnt\n",
		b"ls /a\n",
		b"cat /mnt/marker_hda\n",
		b"cat /a/marker_hdb\n",
		b"cat /proc/mounts\n",
		b"exit\n",
	]
	for c in cmds:
		os.write(fd, c)
		end = time.time() + 2.5
		while time.time() < end:
			r, _, _ = select.select([fd], [], [], 0.2)
			if fd not in r:
				continue
			try:
				chunk = os.read(fd, 8192)
			except OSError:
				break
			if not chunk:
				break
			out.extend(chunk)
			sys.stdout.buffer.write(chunk)
			sys.stdout.buffer.flush()

	time.sleep(0.4)
	try:
		os.close(fd)
	except OSError:
		pass
	try:
		os.waitpid(pid, 0)
	except Exception:
		pass

	return clean(bytes(out)).decode("utf-8", "replace").replace("\r", "")


def listing(text: str, path: str) -> str | None:
	m = re.search(rf"ls {re.escape(path)}\n(.*?)root@tiny", text, re.S)
	return m.group(1) if m else None


def check(text: str) -> list[str]:
	errs: list[str] = []
	mnt = listing(text, "/mnt")
	a = listing(text, "/a")

	if mnt is None:
		errs.append("missing ls /mnt output")
	else:
		if "marker_hda" not in mnt:
			errs.append("/mnt missing marker_hda")
		if "marker_hdb" in mnt:
			errs.append("/mnt unexpectedly has marker_hdb")
		if "hello.txt" not in mnt:
			errs.append("/mnt missing hello.txt")

	if a is None:
		errs.append("missing ls /a output")
	else:
		if "marker_hdb" not in a:
			errs.append("/a missing marker_hdb")
		if "marker_hda" in a:
			errs.append("/a unexpectedly has marker_hda")
		if "hello.txt" in a:
			errs.append("/a unexpectedly has hello.txt")

	if "from-hda" not in text:
		errs.append("cat /mnt/marker_hda did not print from-hda")
	if "from-hdb" not in text:
		errs.append("cat /a/marker_hdb did not print from-hdb")

	if "/dev/hda /mnt ext2" not in text:
		errs.append("/proc/mounts missing hda -> /mnt")
	if "/dev/hdb /a ext2" not in text:
		errs.append("/proc/mounts missing hdb -> /a")

	return errs


def main() -> int:
	if not KERNEL.is_file():
		print(f"FAIL: {KERNEL} not found; run: make -C {ROOT} kernel.elf",
		      file=sys.stderr)
		return 2
	if not MKHD.is_file():
		print(f"FAIL: {MKHD} missing", file=sys.stderr)
		return 2
	for tool in ("mkfs.ext2", "debugfs", QEMU):
		if subprocess.call(["which", tool], stdout=subprocess.DEVNULL,
				   stderr=subprocess.DEVNULL) != 0:
			print(f"FAIL: need `{tool}` on PATH", file=sys.stderr)
			return 2

	with tempfile.TemporaryDirectory(prefix="tinyos-e2e-dual-") as td:
		work = Path(td)
		hda, hdb = prepare_images(work)
		text = run_guest(hda, hdb)

	log = ROOT / "e2e_test" / "last_dual_mount.log"
	log.write_text(text)
	print(f"\nlog: {log}")

	errs = check(text)
	if errs:
		print("==== FAIL ====")
		for e in errs:
			print(" -", e)
		return 1

	print("==== PASS: dual ext2 mounts are isolated ====")
	return 0


if __name__ == "__main__":
	sys.exit(main())
