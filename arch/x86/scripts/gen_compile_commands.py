#!/usr/bin/env python3
"""Generate compile_commands.json for x86 tiny-os (clangd).

自动扫描 ROOT 下全部 .c：目录变动无需再改本脚本。
"""

import argparse
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 跳过的目录名（任意层级）
SKIP_DIRS = {
    ".git",
    ".cursor",
    "__pycache__",
    "node_modules",
}

KERNEL_CFLAGS = [
    "gcc", "-m32", "-ffreestanding", "-fno-pic", "-fno-stack-protector",
    "-nostdlib", "-Wall", "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
    "-c",
]

USER_CFLAGS = KERNEL_CFLAGS + ["-Iuser/include", "-fno-builtin"]

# 宿主工具（tools/），普通用户态 gcc
HOST_CFLAGS = [
    "gcc", "-O2", "-Wall", "-c",
]


def iter_c_sources(root):
    """Yield paths relative to root for every .c under root."""
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(
            d for d in dirnames
            if d not in SKIP_DIRS and not d.startswith(".")
        )
        for name in sorted(filenames):
            if not name.endswith(".c"):
                continue
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root)
            yield rel.replace(os.sep, "/")


def cflags_for(rel_path):
    """Choose compile flags by top-level (or known) directory."""
    top = rel_path.split("/", 1)[0]
    if top == "user":
        return list(USER_CFLAGS)
    if top == "tools":
        return list(HOST_CFLAGS)
    # kernel/、boot/、test/ 及其它树内 C 源：内核 freestanding 标志
    return list(KERNEL_CFLAGS)


def entry(rel_path, cflags):
    src = os.path.join(ROOT, rel_path)
    obj = os.path.splitext(rel_path)[0] + ".o"
    return {
        "directory": ROOT,
        "file": src,
        "arguments": cflags + [src, "-o", obj],
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="list every scanned source")
    args = ap.parse_args()

    sources = list(iter_c_sources(ROOT))
    db = [entry(path, cflags_for(path)) for path in sources]

    out = os.path.join(ROOT, "compile_commands.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump(db, f, indent=2)
        f.write("\n")
    print(f"generated {out} ({len(db)} entries)")
    if args.verbose:
        for path in sources:
            print(f"  {path}")


if __name__ == "__main__":
    main()
