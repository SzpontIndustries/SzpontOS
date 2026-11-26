#!/usr/bin/env python3
"""
Generator archiwum Initramfs w standardowym formacie USTAR dla SzpontOS.
Pakuje zawartość katalogu źródłowego (np. userland/rootfs/) do pojedynczego pliku tar,
z automatyczną optymalizacją rozmiaru:
- usunięcie niepotrzebnych archiwów statycznych (*.a) z /lib
- deduplikacja bibliotek .so i pomocników Git za pomocą dowiązań symbolicznych (symlinks)
- strippowanie symboli debugowania ze wszystkich binariów ELF i bibliotek współdzielonych
"""

import sys
import os
import glob
import shutil
import subprocess
import tarfile

def optimize_rootfs(source_dir):
    """Optymalizuje rozmiar rootfs przed pakowaniem do initramfs."""
    print(f"[*] Optymalizacja rozmiaru rootfs: {source_dir}")

    # 1. Usuń archiwa statyczne (*.a) z lib/ oraz usr/lib/
    for lib_subdir in ["lib", os.path.join("usr", "lib")]:
        for a in glob.glob(os.path.join(source_dir, lib_subdir, "*.a")):
            print(f"  [-] Usunięto zbędne archiwum statyczne: {os.path.basename(a)}")
            try:
                os.remove(a)
            except OSError:
                pass

    # 2. Deduplikacja bibliotek dzielonych (.so) w lib/ oraz usr/lib/
    for lib_subdir in ["lib", os.path.join("usr", "lib")]:
        lib_dir = os.path.join(source_dir, lib_subdir)
        if os.path.isdir(lib_dir):
            for f in os.listdir(lib_dir):
                full = os.path.join(lib_dir, f)
                if os.path.islink(full) or not os.path.isfile(full):
                    continue
                if f.endswith(".so"):
                    matches = [m for m in os.listdir(lib_dir) if m.startswith(f + ".") and os.path.isfile(os.path.join(lib_dir, m)) and not os.path.islink(os.path.join(lib_dir, m)) and not m.endswith(".p")]
                    if matches:
                        target = matches[0]
                        os.remove(full)
                        os.symlink(target, full)
                        print(f"  [SYM] {lib_subdir}/{f} -> {target}")

    # 3. Deduplikacja pomocników Git w usr/libexec/git-core i /bin
    git_core = os.path.join(source_dir, "usr", "libexec", "git-core")
    if os.path.isdir(git_core):
        base_http = os.path.join(git_core, "git-remote-http")
        if os.path.isfile(base_http):
            for name in ["git-remote-https", "git-remote-ftp", "git-remote-ftps", "git-http-fetch"]:
                p = os.path.join(git_core, name)
                if os.path.isfile(p) and not os.path.islink(p):
                    os.remove(p)
                    os.symlink("git-remote-http", p)
                    print(f"  [SYM] git-core/{name} -> git-remote-http")

    bin_dir = os.path.join(source_dir, "bin")
    if os.path.isdir(bin_dir) and os.path.isdir(git_core):
        for name in ["git-remote-http", "git-remote-https", "git-remote-ftp", "git-remote-ftps", "git-http-fetch"]:
            p = os.path.join(bin_dir, name)
            if os.path.isfile(p) and not os.path.islink(p):
                os.remove(p)
                os.symlink("../usr/libexec/git-core/git-remote-http", p)
                print(f"  [SYM] bin/{name} -> ../usr/libexec/git-core/git-remote-http")

    # 4. Deduplikacja grafik w usr/share
    usr_share = os.path.join(source_dir, "usr", "share")
    for f in ["screenshot.png", "szpont-scale.png", "makaljer.png", "szpont-detected.png", "szpont-detected.jpg"]:
        p = os.path.join(usr_share, f)
        art_p = os.path.join(usr_share, "artwork", f)
        if os.path.isfile(p) and not os.path.islink(p) and os.path.exists(art_p):
            os.remove(p)
            os.symlink(os.path.join("artwork", f), p)

    # 5. Strippowanie symboli debugowania ze wszystkich binariów ELF
    strip_bin = shutil.which("x86_64-elf-strip") or shutil.which("strip")
    if strip_bin:
        stripped_count = 0
        for root, dirs, files in os.walk(source_dir):
            for f in files:
                full = os.path.join(root, f)
                if os.path.islink(full) or not os.path.isfile(full) or f.endswith((".sko", ".o")):
                    continue
                try:
                    with open(full, "rb") as fp:
                        magic = fp.read(4)
                    if magic == b"\x7fELF":
                        st = os.stat(full)
                        is_so = ".so" in f
                        cmd = [strip_bin, "-p", "--strip-unneeded", full] if is_so else [strip_bin, "-p", full]
                        subprocess.run(cmd, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                        try:
                            os.utime(full, (st.st_atime, st.st_mtime))
                        except Exception:
                            pass
                        stripped_count += 1
                except Exception:
                    pass
        print(f"  [*] Pomyślnie ostriptowano {stripped_count} plików ELF ({os.path.basename(strip_bin)}).")

def create_initramfs(source_dir, output_file):
    print(f"[*] Generowanie archiwum Initramfs: {output_file} z katalogu: {source_dir}")
    if not os.path.exists(source_dir):
        os.makedirs(source_dir, exist_ok=True)
        os.makedirs(os.path.join(source_dir, "etc"), exist_ok=True)
        with open(os.path.join(source_dir, "etc", "hostname"), "w") as f:
            f.write("szpontos-box\n")
        with open(os.path.join(source_dir, "etc", "issue"), "w") as f:
            f.write("SzpontOS v0.1.0 LTS \\n \\l\n")
        os.makedirs(os.path.join(source_dir, "bin"), exist_ok=True)
        os.makedirs(os.path.join(source_dir, "dev"), exist_ok=True)
        os.makedirs(os.path.join(source_dir, "proc"), exist_ok=True)

    # 0. Weryfikacja integralności rootfs przed rozpoczęciem
    try:
        from verify_rootfs import verify_rootfs
    except ImportError:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from verify_rootfs import verify_rootfs

    if verify_rootfs(source_dir) != 0:
        print("[!] BŁĄD: Walidacja integralności rootfs nie powiodła się! Przerywanie.")
        sys.exit(1)

    # Optymalizacja zawartości przed pakowaniem
    optimize_rootfs(source_dir)

    def fix_ownership(tarinfo):
        """Ustawia właściwe uprawnienia uniksowe dla plików w rootfs."""
        arcname = tarinfo.name
        if arcname in ("home/user", "home/szpont") or arcname.startswith("home/user/") or arcname.startswith("home/szpont/"):
            tarinfo.uid = 1000
            tarinfo.gid = 1000
            tarinfo.uname = "szpont" if "szpont" in arcname else "user"
            tarinfo.gname = "szpont" if "szpont" in arcname else "user"
            if tarinfo.isdir():
                tarinfo.mode = 0o755
        elif arcname in ("tmp", "var/tmp", "tmp/.X11-unix"):
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
            elif arcname in ("bin/sudo", "usr/bin/sudo", "bin/su", "usr/bin/su"):
                tarinfo.mode = 0o4755
            elif (arcname.startswith("bin/") or arcname.startswith("usr/sbin/") or
                  arcname.startswith("usr/bin/") or arcname.startswith("usr/libexec/") or
                  arcname.startswith("etc/rc") or arcname.startswith("etc/rc.d/")):
                tarinfo.mode = 0o755
            elif arcname == "root/.ssh" or arcname.startswith("root/.ssh/"):
                tarinfo.mode = 0o700 if tarinfo.isdir() else 0o600
            elif arcname == "etc/sudoers":
                tarinfo.mode = 0o440
            elif (arcname.startswith("etc/ssh/") and arcname.endswith("_key")) or arcname in ("etc/shadow", "etc/master.passwd"):
                tarinfo.mode = 0o600
        return tarinfo

    with tarfile.open(output_file, "w", format=tarfile.USTAR_FORMAT) as tar:
        for root, dirs, files in os.walk(source_dir):
            for d in dirs:
                full_path = os.path.join(root, d)
                arcname = os.path.relpath(full_path, source_dir)
                tar.add(full_path, arcname=arcname, recursive=False, filter=fix_ownership)
            for f in files:
                full_path = os.path.join(root, f)
                arcname = os.path.relpath(full_path, source_dir)
                tar.add(full_path, arcname=arcname, recursive=False, filter=fix_ownership)
                if arcname.startswith("bin/") or arcname.startswith("lib/") or arcname.endswith(".so"):
                    print(f"  + {arcname}")

    tar_size = os.path.getsize(output_file)
    print(f"[OK] Utworzono poprawnie {output_file} ({tar_size} bajtów / {tar_size / (1024*1024):.2f} MB).")

if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else "userland/rootfs"
    out = sys.argv[2] if len(sys.argv) > 2 else "build/initramfs.tar"
    create_initramfs(src, out)
