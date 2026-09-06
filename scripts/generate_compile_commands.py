#!/usr/bin/env python3
"""
SzpontOS — Compilation Database Generator
(C) Copyright by Szpont Industries. All rights reserved.

Generates a precise compile_commands.json for clangd and VSCode IntelliSense
without needing slow / unreliable execution tracing (bear).
"""

import os
import sys
import json
import shutil

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

def get_compiler():
    # Prefer freestanding cross-compiler, fallback to gcc/clang
    for comp in ["x86_64-elf-gcc", "/opt/homebrew/bin/x86_64-elf-gcc", "/usr/local/bin/x86_64-elf-gcc", "gcc", "clang"]:
        if os.path.isabs(comp) and os.path.exists(comp):
            return comp
        found = shutil.which(comp)
        if found:
            return found
    return "x86_64-elf-gcc"

def main():
    compiler = get_compiler()
    entries = []

    # Common flags
    base_user_includes = [
        "-isystem", os.path.join(ROOT_DIR, "libc/include"),
        "-isystem", os.path.join(ROOT_DIR, "build/sysroot/usr/include"),
        "-I", os.path.join(ROOT_DIR, "compat/linux/include")
    ]

    # Walk through repository directories
    for root, dirs, files in os.walk(ROOT_DIR):
        # Skip build and hidden directories
        if "/build" in root or "/." in root or "/third_party" in root:
            continue

        rel_root = os.path.relpath(root, ROOT_DIR)

        for f in files:
            if not f.endswith(".c"):
                continue

            file_path = os.path.join(root, f)
            rel_file = os.path.relpath(file_path, ROOT_DIR)

            # Determine subsystem
            if rel_file.startswith("kernel/"):
                args = [
                    compiler,
                    "-std=c17",
                    "-ffreestanding",
                    "-fno-builtin",
                    "-nostdlib",
                    "-mno-red-zone",
                    "-mno-mmx",
                    "-mno-sse",
                    "-mno-sse2",
                    "-mcmodel=kernel",
                    "-fno-stack-protector",
                    "-fno-pic",
                    "-fno-pie",
                    "-mgeneral-regs-only",
                    "-Wall",
                    "-Wextra",
                    "-I" + os.path.join(ROOT_DIR, "kernel/include"),
                    "-I" + os.path.join(ROOT_DIR, "kernel/src"),
                    "-I" + os.path.join(ROOT_DIR, "kernel/include/arch/x86_64"),
                    "-D__x86_64__=1",
                    "-D__szpontos__=1",
                    "-D__KERNEL__=1",
                    "-c", rel_file,
                    "-o", os.path.join("build", rel_file[:-2] + ".o")
                ]
            elif rel_file.startswith("libc/"):
                args = [
                    compiler,
                    "-std=c17",
                    "-ffreestanding",
                    "-fno-builtin",
                    "-nostdlib",
                    "-mno-red-zone",
                    "-fno-stack-protector",
                    "-fPIC",
                    "-Wall",
                    "-Wextra",
                    "-I" + os.path.join(ROOT_DIR, "libc/include"),
                    "-D__x86_64__=1",
                    "-D__szpontos__=1",
                    "-D_POSIX_C_SOURCE=200809L",
                    "-D_GNU_SOURCE=1",
                    "-D_BSD_SOURCE=1",
                    "-c", rel_file,
                    "-o", os.path.join("build", rel_file[:-2] + ".o")
                ]
            elif rel_file.startswith("modules/"):
                args = [
                    compiler,
                    "-std=c17",
                    "-ffreestanding",
                    "-fno-builtin",
                    "-nostdlib",
                    "-mno-red-zone",
                    "-mno-mmx",
                    "-mno-sse",
                    "-mno-sse2",
                    "-mcmodel=large",
                    "-fno-stack-protector",
                    "-fPIC",
                    "-fno-common",
                    "-mgeneral-regs-only",
                    "-Wall",
                    "-Wextra",
                    "-I" + os.path.join(ROOT_DIR, "kernel/include"),
                    "-D__x86_64__=1",
                    "-D__szpontos__=1",
                    "-D__MODULE__=1",
                    "-c", rel_file,
                    "-o", os.path.join("build", rel_file[:-2] + ".sko")
                ]
            elif rel_file.startswith("userland/"):
                extra_inc = []
                if "xserver" in rel_file:
                    extra_inc.extend([
                        "-I" + os.path.join(ROOT_DIR, "userland/xserver/include"),
                        "-I" + os.path.join(ROOT_DIR, "build/sysroot/usr/include/pixman-1"),
                        "-I" + os.path.join(ROOT_DIR, "build/sysroot/usr/include/X11")
                    ])
                if "desktop" in rel_file:
                    extra_inc.extend([
                        "-I" + os.path.join(ROOT_DIR, "userland/desktop/include"),
                        "-I" + os.path.join(ROOT_DIR, "build/sysroot/usr/include/X11")
                    ])

                args = [
                    compiler,
                    "-std=c17",
                    "-ffreestanding",
                    "-fno-builtin",
                    "-nostdlib",
                    "-mno-red-zone",
                    "-fno-stack-protector",
                    "-fPIC",
                    "-Wall",
                    "-Wextra",
                    *base_user_includes,
                    *extra_inc,
                    "-D__x86_64__=1",
                    "-D__szpontos__=1",
                    "-D_POSIX_C_SOURCE=200809L",
                    "-D_GNU_SOURCE=1",
                    "-D_BSD_SOURCE=1",
                    "-c", rel_file,
                    "-o", os.path.join("build", rel_file[:-2] + ".o")
                ]
            else:
                args = [
                    compiler,
                    "-std=c17",
                    "-ffreestanding",
                    "-fno-builtin",
                    "-Wall",
                    "-Wextra",
                    "-I" + os.path.join(ROOT_DIR, "kernel/include"),
                    "-I" + os.path.join(ROOT_DIR, "libc/include"),
                    "-D__x86_64__=1",
                    "-D__szpontos__=1",
                    "-c", rel_file,
                    "-o", os.path.join("build", rel_file[:-2] + ".o")
                ]

            entries.append({
                "directory": ROOT_DIR,
                "file": file_path,
                "arguments": args,
                "output": os.path.join(ROOT_DIR, "build", rel_file[:-2] + ".o")
            })

    output_path = os.path.join(ROOT_DIR, "compile_commands.json")
    with open(output_path, "w") as f:
        json.dump(entries, f, indent=2)

    print(f"  [OK] Wygenerowano {len(entries)} wpisów kompilacji w {output_path}")

if __name__ == "__main__":
    main()
