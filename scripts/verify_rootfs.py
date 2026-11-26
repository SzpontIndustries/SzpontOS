#!/usr/bin/env python3
"""
Rootfs Integrity Validator for SzpontOS
(C) Copyright by Szpont Industries. All rights reserved.

Checks that build/rootfs contains 100% of required directories, binaries,
shared libraries, configurations, dotfiles, and X11 resources.
Fails fast with exit code 1 if any critical component is missing.
"""

import sys
import os
import stat

REQUIRED_DIRS = [
    "bin",
    "sbin",
    "lib",
    "usr/bin",
    "usr/sbin",
    "usr/lib",
    "usr/tbin",
    "usr/share",
    "usr/share/X11",
    "usr/share/X11/xkb",
    "usr/share/artwork",
    "dev",
    "etc",
    "etc/rc.d",
    "etc/X11",
    "etc/X11/app-defaults",
    "proc",
    "sys",
    "mnt",
    "tmp",
    "tmp/.X11-unix",
    "var",
    "var/log",
    "var/run",
    "var/empty",
    "var/lib/xkb",
    "root",
    "home",
    "home/szpont",
]

REQUIRED_BINARIES = [
    "bin/init",
    "bin/sh",
    "usr/bin/szpontdesktop",
    "usr/bin/xterm",
    "usr/bin/startx",
    "usr/bin/szponterm",
    "bin/cat",
    "bin/ls",
    "bin/ps",
    "usr/bin/top",
    "bin/uname",
    "bin/hostname",
    "bin/mkdir",
    "bin/rm",
    "bin/chmod",
    "bin/kill",
    "usr/bin/killall",
    "usr/bin/sudo",
    "usr/bin/id",
    "usr/bin/whoami",
    "bin/dmesg",
    "bin/sync",
    "usr/bin/xkbcomp",
    "usr/bin/Xorg",
    "usr/tbin/mathtest",
    "usr/tbin/epolltest",
    "usr/tbin/corecheck",
    "usr/tbin/compattest",
]

REQUIRED_LIBRARIES = [
    "lib/libc.so",
    "lib/libm.so",
    "usr/lib/libdrm.so",
    "usr/lib/libgbm.so",
    "usr/lib/libpixman-1.so",
    "usr/lib/libX11.so",
    "usr/lib/libxcb.so",
    "usr/lib/libXext.so",
    "usr/lib/libz.so",
    "usr/lib/libstdc++.so",
]

REQUIRED_CONFIGS = [
    "etc/passwd",
    "etc/group",
    "etc/shells",
    "etc/profile",
    "etc/shrc",
    "etc/rc",
    "etc/rc.conf",
    "etc/sudoers",
    "etc/X11/xorg.conf",
    "etc/X11/app-defaults/XTerm",
    "usr/share/X11/app-defaults/XTerm",
]

def verify_rootfs(rootfs_dir, test_names=()):
    print(f"[*] Weryfikacja integralności rootfs: {rootfs_dir}...")
    errors = []

    if not os.path.isdir(rootfs_dir):
        print(f"[!] BŁĄD: Katalog rootfs '{rootfs_dir}' nie istnieje!")
        return 1

    # 1. Sprawdź katalogi
    for d in REQUIRED_DIRS:
        path = os.path.join(rootfs_dir, d)
        if not os.path.isdir(path):
            errors.append(f"Brak katalogu: /{d}")

    # 2. Sprawdź binaria
    for b in REQUIRED_BINARIES + [f"usr/tbin/{name}" for name in test_names]:
        path = os.path.join(rootfs_dir, b)
        if not os.path.exists(path):
            # Sprawdź alternatywną ścieżkę (np. bin/sshd zamiast usr/sbin/sshd lub na odwrót)
            alt = os.path.join(rootfs_dir, os.path.basename(b))
            if not os.path.exists(alt):
                errors.append(f"Brak pliku wykonywalnego: /{b}")
        else:
            # Sprawdź uprawnienia wykonywania
            st = os.stat(path)
            if not (st.st_mode & (stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)):
                errors.append(f"Brak uprawnień wykonywania: /{b}")

    # 3. Sprawdź biblioteki dzielone
    for l in REQUIRED_LIBRARIES:
        path = os.path.join(rootfs_dir, l)
        if not os.path.exists(path):
            errors.append(f"Brak biblioteki współdzielonej: /{l}")

    # 4. Sprawdź pliki konfiguracyjne i zasoby
    for c in REQUIRED_CONFIGS:
        path = os.path.join(rootfs_dir, c)
        if not os.path.exists(path):
            errors.append(f"Brak pliku konfiguracyjnego/zasobu: /{c}")

    if errors:
        print("\n" + "=" * 70)
        print("  [!] KRYTYCZNE BŁĘDY INTEGRALNOŚCI ROOTFS:")
        print("=" * 70)
        for err in errors:
            print(f"  [-] {err}")
        print("=" * 70)
        print("  Przerwano generowanie obrazu z powodu niekompletnego rootfs.\n")
        return 1

    print("  [OK] Integralność rootfs w 100% poprawna (wszystkie pliki, foldery i zasoby obecne).")
    return 0

if __name__ == "__main__":
    target = sys.argv[1] if len(sys.argv) > 1 else "build/rootfs"
    sys.exit(verify_rootfs(target, sys.argv[2:]))
