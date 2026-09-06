#!/usr/bin/env python3
"""
Test weryfikacyjny: shutdown & reboot w SzpontOS.
"""

import subprocess
import time
import os
import sys
import select
import re

def test_command(cmd, expected_log):
    qemu_cmd = [
        "qemu-system-x86_64",
        "-M", "q35",
        "-cpu", "max",
        "-m", "512M",
        "-display", "none",
        "-cdrom", "build/szpontos.iso",
        "-serial", "stdio"
    ]

    print(f"[TEST] Testowanie polecenia '{cmd}' w SzpontOS...")
    proc = subprocess.Popen(
        qemu_cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT
    )

    output = ""
    start_time = time.time()
    commands_sent = False

    try:
        while time.time() - start_time < 20:
            r, _, _ = select.select([proc.stdout], [], [], 0.1)
            if r:
                chunk = os.read(proc.stdout.fileno(), 1024).decode('utf-8', errors='ignore')
                if not chunk:
                    break
                output += chunk
                sys.stdout.write(chunk)
                sys.stdout.flush()

                clean_chunk = re.sub(r'\x1b\[[0-9;?]*[a-zA-Z]', '', output)

                if not commands_sent and "root@szpontos-box:/" in clean_chunk:
                    time.sleep(0.5)
                    print(f"\n[TEST] Wysyłanie polecenia {cmd}...")
                    proc.stdin.write((cmd + "\n").encode('utf-8'))
                    proc.stdin.flush()
                    commands_sent = True

                if expected_log in clean_chunk:
                    time.sleep(0.5)
                    break

    finally:
        proc.kill()
        proc.wait()

    clean_output = re.sub(r'\x1b\[[0-9;?]*[a-zA-Z]', '', output)
    passed = expected_log in clean_output

    if passed:
        print(f"\n[PASSED] Polecenie '{cmd}' działa poprawnie!")
        return True
    else:
        print(f"\n[FAILED] Polecenie '{cmd}' nie powiodło się.")
        return False

if __name__ == "__main__":
    ok_shutdown = test_command("shutdown", "System is shutting down...")
    ok_reboot = test_command("reboot", "System is rebooting...")

    if ok_shutdown and ok_reboot:
        print("\n[OK] Wszystkie testy shutdown i reboot zakończone sukcesem!")
        sys.exit(0)
    else:
        sys.exit(1)
