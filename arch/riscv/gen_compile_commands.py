#!/usr/bin/env python3
"""Emit compile_commands.json for clangd (kernel, user, and arch translation units)."""
import glob
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))


def make_var(cwd: str, name: str) -> str:
    out = subprocess.check_output(
        ["make", "-s", f"print-{name}"],
        text=True,
        cwd=cwd,
    )
    return out.strip()


def resolve_cc(cc: str) -> str:
    cc_abs = shutil.which(cc) or cc
    if cc_abs == cc and os.path.basename(cc) == cc:
        print(
            "gen_compile_commands: warning: compiler not resolved to an absolute "
            f"path ({cc!r}); clangd may fail until this binary is on PATH.",
            file=sys.stderr,
        )
    return cc_abs


def source_cflags(src_abs: str, cflags: list[str]) -> list[str]:
    if src_abs.endswith(".S"):
        return ["-x", "assembler-with-cpp"] + cflags
    return cflags


def add_entries(
    db: list,
    *,
    directory: str,
    cc: str,
    cflags: list[str],
    sources: list[str],
) -> None:
    cc_abs = resolve_cc(cc)
    for src in sources:
        src_abs = src if os.path.isabs(src) else os.path.join(ROOT, src)
        rel = os.path.relpath(src_abs, directory)
        obj_abs = os.path.join(directory, rel[:-2] + ".o")
        arguments = (
            [cc_abs]
            + source_cflags(src_abs, cflags)
            + ["-c", "-o", obj_abs, src_abs]
        )
        db.append(
            {
                "directory": directory,
                "arguments": arguments,
                "file": src_abs,
            }
        )


def arch_make_dirs() -> list[str]:
    arch_root = os.path.join(ROOT, "arch")
    if not os.path.isdir(arch_root):
        return []
    dirs = []
    for name in sorted(os.listdir(arch_root)):
        sub = os.path.join(arch_root, name)
        if os.path.isdir(sub) and os.path.isfile(os.path.join(sub, "Makefile")):
            dirs.append(sub)
    return dirs


def arch_sources(arch_dir: str) -> list[str]:
    sources: list[str] = []
    for dirpath, _, filenames in os.walk(arch_dir):
        for name in filenames:
            if name.endswith((".c", ".S")):
                sources.append(os.path.join(dirpath, name))
    return sorted(sources)


def main() -> int:
    os.chdir(ROOT)
    db: list = []

    cc = make_var(ROOT, "cc")
    cflags = make_var(ROOT, "cflags").split()
    riscv_sources = sorted(glob.glob("kernel/*.c") + glob.glob("user/*.c"))
    add_entries(db, directory=ROOT, cc=cc, cflags=cflags, sources=riscv_sources)

    for arch_dir in arch_make_dirs():
        arch_cc = make_var(arch_dir, "cc")
        arch_cflags = make_var(arch_dir, "cflags").split()
        try:
            arch_user_cflags = make_var(arch_dir, "user-cflags").split()
        except subprocess.CalledProcessError:
            arch_user_cflags = arch_cflags
        arch_srcs = arch_sources(arch_dir)
        kernel_srcs = [
            s for s in arch_srcs if "/user/" not in s.replace("\\", "/")
        ]
        user_srcs = [
            s for s in arch_srcs if "/user/" in s.replace("\\", "/")
        ]
        add_entries(
            db,
            directory=arch_dir,
            cc=arch_cc,
            cflags=arch_cflags,
            sources=kernel_srcs,
        )
        add_entries(
            db,
            directory=arch_dir,
            cc=arch_cc,
            cflags=arch_user_cflags,
            sources=user_srcs,
        )
    path = os.path.join(ROOT, "compile_commands.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(db, f, indent=2)
        f.write("\n")
    print(f"wrote {path} ({len(db)} entries)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
