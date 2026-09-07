#!/usr/bin/env python3
"""
Generator archiwum Initramfs w standardowym formacie USTAR dla SzpontOS.
Pakuje zawartość katalogu źródłowego (np. userland/rootfs/) do pojedynczego pliku tar.
"""

import sys
import os
import tarfile

def create_initramfs(source_dir, output_file):
    print(f"[*] Generowanie archiwum Initramfs: {output_file} z katalogu: {source_dir}")
    if not os.path.exists(source_dir):
        os.makedirs(source_dir, exist_ok=True)
        # Create default placeholder files
        with open(os.path.join(source_dir, "welcome.txt"), "w") as f:
            f.write("Witaj w systemie SzpontOS!\n(C) Copyright by Szpont Industries. All rights reserved.\nTen plik pochodzi z ramdysku Initramfs (USTAR).\n")
        os.makedirs(os.path.join(source_dir, "etc"), exist_ok=True)
        with open(os.path.join(source_dir, "etc", "hostname"), "w") as f:
            f.write("szpontos-box\n")
        with open(os.path.join(source_dir, "etc", "issue"), "w") as f:
            f.write("SzpontOS v0.1.0 LTS \\n \\l\n")
        os.makedirs(os.path.join(source_dir, "bin"), exist_ok=True)
        os.makedirs(os.path.join(source_dir, "dev"), exist_ok=True)
        os.makedirs(os.path.join(source_dir, "proc"), exist_ok=True)

    def fix_ownership(tarinfo):
        """Set proper Unix ownership and permissions for SzpontOS rootfs."""
        arcname = tarinfo.name
        # /home/user and /home/szpont and their contents belong to uid=1000, gid=1000
        if arcname in ("home/user", "home/szpont") or arcname.startswith("home/user/") or arcname.startswith("home/szpont/"):
            tarinfo.uid = 1000
            tarinfo.gid = 1000
            tarinfo.uname = "szpont" if "szpont" in arcname else "user"
            tarinfo.gname = "szpont" if "szpont" in arcname else "user"
            if tarinfo.isdir():
                tarinfo.mode = 0o755
        elif arcname == "tmp" or arcname == "var/tmp":
            tarinfo.uid = 0
            tarinfo.gid = 0
            tarinfo.uname = "root"
            tarinfo.gname = "root"
            tarinfo.mode = 0o1777
        else:
            tarinfo.uid = 0
            tarinfo.gid = 0
            tarinfo.uname = "root"
            tarinfo.gname = "root"
            if tarinfo.isdir():
                tarinfo.mode = 0o755
            elif arcname.startswith("bin/") or arcname.startswith("usr/sbin/") or arcname.startswith("usr/bin/") or arcname.startswith("usr/libexec/") or arcname.startswith("etc/rc") or arcname.startswith("etc/rc.d/"):
                tarinfo.mode = 0o755
            elif arcname == "root/.ssh" or arcname.startswith("root/.ssh/"):
                tarinfo.mode = 0o700 if tarinfo.isdir() else 0o600
            elif (arcname.startswith("etc/ssh/") and arcname.endswith("_key")) or arcname in ("etc/shadow", "etc/master.passwd"):
                tarinfo.mode = 0o600
        return tarinfo

    with tarfile.open(output_file, "w", format=tarfile.USTAR_FORMAT) as tar:
        for root, dirs, files in os.walk(source_dir):
            for d in dirs:
                full_path = os.path.join(root, d)
                arcname = os.path.relpath(full_path, source_dir)
                tar.add(full_path, arcname=arcname, recursive=False, filter=fix_ownership)
                print(f"  + {arcname}/")
            for f in files:
                full_path = os.path.join(root, f)
                arcname = os.path.relpath(full_path, source_dir)
                tar.add(full_path, arcname=arcname, recursive=False, filter=fix_ownership)
                print(f"  + {arcname}")
    print(f"[OK] Utworzono poprawnie {output_file} ({os.path.getsize(output_file)} bajtów).")

if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else "userland/rootfs"
    out = sys.argv[2] if len(sys.argv) > 2 else "build/initramfs.tar"
    create_initramfs(src, out)
