#!/usr/bin/env python3
"""
SzpontOS — Compilation Database Generator
(C) Copyright by Szpont Industries. All rights reserved.

Generates a precise compile_commands.json for clangd and VSCode IntelliSense
without needing slow / unreliable execution tracing (bear).

Flag sets below intentionally mirror config.mk (CFLAGS / USER_CFLAGS /
USER_CXXFLAGS / MODULE_CFLAGS) plus the per-component -I/-D extras from
kernel/Makefile, libc/Makefile, modules/Makefile and userland/*/Makefile,
so the linter parses exactly what the build compiles. A small include
superset per userland component is deliberate: extra -I never hurts clangd,
a missing one floods it with errors.
"""

import os
import sys
import json
import shutil

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SYSROOT_INC = os.path.join(ROOT_DIR, "build/sysroot/usr/include")

C_EXTS = {".c"}
CXX_EXTS = {".cpp", ".cc", ".cxx", ".C", ".c++"}


def get_compiler():
    # Prefer freestanding cross-compiler (same choice as config.mk), no ccache.
    for comp in ["x86_64-elf-gcc", "/opt/homebrew/bin/x86_64-elf-gcc",
                 "/usr/local/bin/x86_64-elf-gcc", "gcc", "clang"]:
        if os.path.isabs(comp) and os.path.exists(comp):
            return comp
        found = shutil.which(comp)
        if found:
            return found
    return "x86_64-elf-gcc"


def get_cxx_compiler(cc):
    # C++ driver matching the C one so clangd picks C++ mode (g++ basename).
    cands = ["x86_64-elf-g++", "/opt/homebrew/bin/x86_64-elf-g++",
             "/usr/local/bin/x86_64-elf-g++"]
    if cc.endswith("clang"):
        cands.append("clang++")
    cands += ["g++", "c++"]
    for comp in cands:
        if os.path.isabs(comp) and os.path.exists(comp):
            return comp
        found = shutil.which(comp)
        if found:
            return found
    return cc


def base_c_args(cc):
    # Mirrors USER_CFLAGS from config.mk.
    return [
        cc,
        "-std=c17",
        "-D__szpontos__",
        "-D__unix__",
        "-ffreestanding",
        "-fno-stack-protector",
        "-fno-stack-check",
        "-fno-lto",
        "-fPIC",
        "-mno-red-zone",
        "-Wall",
        "-Wextra",
        "-O2",
        "-g",
        "-isystem", os.path.join(ROOT_DIR, "libc/include"),
        "-isystem", os.path.join(ROOT_DIR, "kernel/include/uapi"),
        "-isystem", SYSROOT_INC,
        "-I" + os.path.join(ROOT_DIR, "libc/include"),
        "-I" + SYSROOT_INC,
        "-D__x86_64__=1",
        "-D__szpontos__=1",
        "-D_POSIX_C_SOURCE=200809L",
        "-D_GNU_SOURCE=1",
        "-D_BSD_SOURCE=1",
    ]


def base_cxx_args(cxx):
    # Mirrors USER_CXXFLAGS from config.mk.
    return [
        cxx,
        "-std=gnu++17",
        "-D__szpontos__",
        "-D__unix__",
        "-ffreestanding",
        "-fno-stack-protector",
        "-fno-stack-check",
        "-fno-lto",
        "-fPIC",
        "-mno-red-zone",
        "-Wall",
        "-Wextra",
        "-O2",
        "-g",
        "-nostdinc++",
        "-isystem", os.path.join(SYSROOT_INC, "c++"),
        "-isystem", os.path.join(SYSROOT_INC, "c++", "bits"),
        "-isystem", os.path.join(SYSROOT_INC, "c++", "backward"),
        "-isystem", os.path.join(SYSROOT_INC, "c++", "ext"),
        "-isystem", SYSROOT_INC,
        "-I" + SYSROOT_INC,
        "-D__x86_64__=1",
        "-D__szpontos__=1",
        "-D_POSIX_C_SOURCE=200809L",
        "-D_GNU_SOURCE=1",
        "-D_BSD_SOURCE=1",
    ]


def kernel_c_args(cc):
    # Mirrors CFLAGS from config.mk (kernel build).
    return [
        cc,
        "-std=c17",
        "-D__KERNEL__=1",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-stack-protector",
        "-fno-stack-check",
        "-fno-lto",
        "-fno-pie",
        "-fno-pic",
        "-mno-80387",
        "-mno-mmx",
        "-mno-sse",
        "-mno-sse2",
        "-mno-red-zone",
        "-mcmodel=kernel",
        "-Wall",
        "-Wextra",
        "-O2",
        "-g",
        "-I" + os.path.join(ROOT_DIR, "kernel/include"),
        "-I" + os.path.join(ROOT_DIR, "kernel/include/uapi"),
        "-I" + os.path.join(ROOT_DIR, "build/include"),
        "-I" + ROOT_DIR,
        "-D__x86_64__=1",
    ]


def module_c_args(cc):
    # Mirrors MODULE_CFLAGS = CFLAGS + (-mcmodel=kernel -fno-pic -fno-pie),
    # which CFLAGS already carries, so identical to the kernel set.
    return kernel_c_args(cc)


# Include superset shared by graphical userland components (X11 / UI stack).
X11_SYSROOT_ISYSTEM = [
    "-isystem", os.path.join(SYSROOT_INC, "freetype2"),
    "-isystem", os.path.join(SYSROOT_INC, "harfbuzz"),
    "-isystem", os.path.join(SYSROOT_INC, "pixman-1"),
    "-isystem", os.path.join(SYSROOT_INC, "libdrm"),
]
X11_SYSROOT_I = [
    "-I" + os.path.join(ROOT_DIR, "userland/lib/libszpontui/include"),
    "-I" + os.path.join(ROOT_DIR, "third_party/stb"),
    "-I" + SYSROOT_INC,
    "-I" + os.path.join(SYSROOT_INC, "freetype2"),
    "-I" + os.path.join(SYSROOT_INC, "harfbuzz"),
    "-I" + os.path.join(SYSROOT_INC, "pixman-1"),
]


def args_for(rel_file, cc, cxx):
    ext = os.path.splitext(rel_file)[1]
    is_cxx = ext in CXX_EXTS
    comp = cxx if is_cxx else cc
    base = (base_cxx_args(comp) if is_cxx else base_c_args(comp))
    extra = []

    if rel_file.startswith("kernel/"):
        return kernel_c_args(comp), comp
    if rel_file.startswith("modules/"):
        return module_c_args(comp), comp
    if rel_file.startswith("libc/"):
        return base, comp
    if rel_file.startswith("userland/sh/"):
        extra = ["-DSHELL", "-DNO_HISTORY",
                 "-I" + os.path.join(ROOT_DIR, "userland/sh")]
        return base + extra, comp
    if rel_file.startswith("userland/xserver/"):
        extra = ["-I" + os.path.join(ROOT_DIR, "userland/xserver/include")]
        return base + extra + X11_SYSROOT_ISYSTEM, comp
    if rel_file.startswith("userland/lib/libszpontui/"):
        extra = ["-I" + os.path.join(ROOT_DIR,
                                     "userland/lib/libszpontui/include")]
        return base + extra + X11_SYSROOT_ISYSTEM, comp
    if rel_file.startswith("userland/szpontdesktop/"):
        extra = ["-I" + os.path.join(ROOT_DIR, "userland/szpontdesktop/include")]
        return base + extra + X11_SYSROOT_ISYSTEM + X11_SYSROOT_I, comp
    if rel_file.startswith("userland/"):
        # bin/ (*.c and *.cpp: cpptest, szpontui_demo), init/, szpontlogin/,
        # szponter/, szpontview/, szpontmon/ + scripts/tests use the same UI
        # include superset; harmless where unused, vital where needed
        # (stb for makaljer, libdrm for drmtest, SzpontUI for desktops).
        return base + X11_SYSROOT_ISYSTEM + X11_SYSROOT_I, comp
    if rel_file.startswith("scripts/"):
        # scripts/tests/corecheck.c builds freestanding with USER_CFLAGS,
        # but *_regression.c unit-test kernel internals directly
        # (#include <kernel/types.h> + #include "kernel/src/....c") and are
        # not part of the build - kernel flags are the only ones that parse.
        if "regression" in os.path.basename(rel_file):
            return kernel_c_args(comp), comp
        return base, comp
    # Anything else: kernel+libc includes, freestanding C/C++.
    extra = ["-I" + os.path.join(ROOT_DIR, "kernel/include"),
             "-I" + os.path.join(ROOT_DIR, "libc/include")]
    return base + extra, comp


def main():
    cc = get_compiler()
    cxx = get_cxx_compiler(cc)
    entries = []

    for root, dirs, files in os.walk(ROOT_DIR):
        # Skip build output, hidden dirs, third-party ports (own build
        # systems; clangd would drown in their generated headers) and the
        # limine host tool (built natively with $CC=cc, not freestanding).
        if ("/build" in root or "/." in root or "/third_party" in root
                or "/limine-bin" in root):
            continue

        for f in files:
            ext = os.path.splitext(f)[1]
            if ext not in C_EXTS and ext not in CXX_EXTS:
                continue

            file_path = os.path.join(root, f)
            rel_file = os.path.relpath(file_path, ROOT_DIR)
            args, _ = args_for(rel_file, cc, cxx)
            args = args + ["-c", rel_file,
                           "-o", os.path.join("build", rel_file[: -len(ext)] + ".o")]

            entries.append({
                "directory": ROOT_DIR,
                "file": file_path,
                "arguments": args,
                "output": os.path.join(ROOT_DIR, "build",
                                       rel_file[: -len(ext)] + ".o"),
            })

    output_path = os.path.join(ROOT_DIR, "compile_commands.json")
    with open(output_path, "w") as f:
        json.dump(entries, f, indent=2)

    n_cxx = sum(1 for e in entries
                if os.path.splitext(e["file"])[1] in CXX_EXTS)
    print(f"  [OK] Wygenerowano {len(entries)} wpisow kompilacji "
          f"({n_cxx} C++) w {output_path} (CC={cc}, CXX={cxx})")


if __name__ == "__main__":
    main()
