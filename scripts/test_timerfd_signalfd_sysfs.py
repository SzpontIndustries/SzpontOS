#!/usr/bin/env python3
"""
Automated verification test for timerfd, signalfd, sysfs, and PTY fixes in SzpontOS.
"""

import subprocess
import time
import os
import sys
import select
import re

def run_test():
    qemu_cmd = [
        "qemu-system-x86_64",
        "-M", "q35,i8042=on",
        "-cpu", "max",
        "-smp", "4",
        "-m", "512M",
        "-display", "none",
        "-cdrom", "build/szpontos.iso",
        "-serial", "stdio",
        "-no-reboot",
        "-no-shutdown"
    ]

    print("[TEST] Launching QEMU headless to test timerfd, signalfd, and sysfs...")
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
        while time.time() - start_time < 45:
            r, _, _ = select.select([proc.stdout], [], [], 0.1)
            if r:
                chunk = os.read(proc.stdout.fileno(), 1024).decode('utf-8', errors='ignore')
                if not chunk:
                    break
                output += chunk
                sys.stdout.write(chunk)
                sys.stdout.flush()

                clean_output = re.sub(r'\x1b\[[0-9;?]*[a-zA-Z]', '', output)

                if not commands_sent and "root@szpontos-box" in clean_output and "Type 'help'" in clean_output:
                    time.sleep(0.5)
                    print("\n[TEST] Sending test commands: timerfdtest, signalfdtest, sysfstest...")
                    commands = [
                        "/usr/tbin/timerfdtest",
                        "/usr/tbin/signalfdtest",
                        "/usr/tbin/sysfstest",
                        "echo ALL_SUBSYSTEM_TESTS_COMPLETED\n"
                    ]
                    for cmd in commands:
                        proc.stdin.write((cmd + "\n").encode('utf-8'))
                        proc.stdin.flush()
                        time.sleep(0.3)
                    commands_sent = True

                if "=== COMPATIBILITY TEST SUITE COMPLETE ===" in clean_output or "ALL_SUBSYSTEM_TESTS_COMPLETED" in clean_output:
                    print("\n[TEST] Detected completion token!")
                    time.sleep(1)
                    break
    finally:
        proc.kill()
        proc.wait()

    clean_output = re.sub(r'\x1b\[[0-9;?]*[a-zA-Z]', '', output)

    passed_timerfd = "All timerfd(2) tests PASSED successfully!" in clean_output
    passed_signalfd = "All signalfd(2) tests PASSED successfully!" in clean_output
    passed_sysfs = "All SysFS (/sys) tests PASSED successfully!" in clean_output

    print("\n" + "="*50)
    print(f"timerfd test:  {'PASS' if passed_timerfd else 'FAIL'}")
    print(f"signalfd test: {'PASS' if passed_signalfd else 'FAIL'}")
    print(f"sysfs test:    {'PASS' if passed_sysfs else 'FAIL'}")
    print("="*50)

    if passed_timerfd and passed_signalfd and passed_sysfs:
        print("[SUCCESS] All new Linux subsystems verified and operational!")
        return 0
    else:
        print("[ERROR] One or more tests failed to complete.")
        return 1

if __name__ == "__main__":
    sys.exit(run_test())
