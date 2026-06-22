#!/usr/bin/env python3
"""Emit compile_commands.json for clangd (kernel + user RISC-V translation units)."""
import glob
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))


def make_var(name: str) -> str:
    out = subprocess.check_output(
        ["make", "-s", f"print-{name}"],
        text=True,
        cwd=ROOT,
    )
    return out.strip()


def main() -> int:
    os.chdir(ROOT)
    cc = make_var("cc")
    # Absolute compiler path so clangd QueryDriver works when the IDE's PATH
    # differs from the shell where `make` was run.
    cc_abs = shutil.which(cc) or cc
    if cc_abs == cc and os.path.basename(cc) == cc:
        print(
            "gen_compile_commands: warning: compiler not resolved to an absolute "
            f"path ({cc!r}); clangd may fail until this binary is on PATH.",
            file=sys.stderr,
        )
    cflags = make_var("cflags").split()
    files = sorted(glob.glob("kernel/*.c") + glob.glob("user/*.c"))
    db = []
    for src in files:
        obj = src[:-2] + ".o"
        src_abs = os.path.join(ROOT, src)
        obj_abs = os.path.join(ROOT, obj)
        arguments = [cc_abs] + cflags + ["-c", "-o", obj_abs, src_abs]
        db.append(
            {
                "directory": ROOT,
                "arguments": arguments,
                "file": src_abs,
            }
        )
    path = os.path.join(ROOT, "compile_commands.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(db, f, indent=2)
        f.write("\n")
    print(f"wrote {path} ({len(db)} entries)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
