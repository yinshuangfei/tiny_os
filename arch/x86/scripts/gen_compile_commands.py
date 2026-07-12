#!/usr/bin/env python3
"""Generate compile_commands.json for x86 tiny-os (clangd)."""

import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

KERNEL_CFLAGS = [
    "gcc", "-m32", "-ffreestanding", "-fno-pic", "-fno-stack-protector",
    "-nostdlib", "-Wall", "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
    "-c",
]

USER_CFLAGS = KERNEL_CFLAGS + ["-Iuser", "-fno-builtin"]

KERNEL_SOURCES = [
    "kernel/main.c", "kernel/mem.c", "kernel/serial.c", "kernel/timer.c",
    "kernel/gdt.c", "kernel/idt.c", "kernel/trapstack.c", "kernel/cpu.c",
    "kernel/spinlock.c", "kernel/printf.c", "kernel/utils.c",
    "kernel/buddy.c", "kernel/slab.c",     "kernel/vm.c", "kernel/proc.c", "kernel/task_queue.c",
    "kernel/init.c", "kernel/debug.c", "kernel/exception.c",
    "kernel/syscall.c", "kernel/sysproc.c", "kernel/execve.c",
    "test/kernel_test.c",
]

USER_SOURCES = ["user/initcode.c"]


def entry(rel_path, cflags):
    src = os.path.join(ROOT, rel_path)
    obj = os.path.splitext(rel_path)[0] + ".o"
    return {
        "directory": ROOT,
        "file": src,
        "arguments": cflags + [src, "-o", obj],
    }


def main():
    db = []
    for path in KERNEL_SOURCES:
        db.append(entry(path, list(KERNEL_CFLAGS)))
    for path in USER_SOURCES:
        db.append(entry(path, list(USER_CFLAGS)))

    out = os.path.join(ROOT, "compile_commands.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump(db, f, indent=2)
        f.write("\n")
    print(f"generated {out} ({len(db)} entries)")


if __name__ == "__main__":
    main()
