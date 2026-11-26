#!/usr/bin/env python3
"""
Automated verification test for PTY signal propagation, Ctrl+C (0x03) isolation,
and process group handling in SzpontOS.
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

    print("[TEST] Launching QEMU headless to test PTY signal isolation and process groups...")
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
                    print("\n[TEST] Shell ready. Sending '/usr/tbin/ptytest' command...")
                    commands = [
                        "/usr/tbin/ptytest",
                        "echo SIGNAL_TEST_FINISHED\n"
                    ]
                    for cmd in commands:
                        proc.stdin.write((cmd + "\n").encode('utf-8'))
                        proc.stdin.flush()
                        time.sleep(0.5)
                    commands_sent = True

                if "SIGNAL_TEST_FINISHED" in clean_output:
                    print("\n[TEST] Detected completion token!")
                    time.sleep(1)
                    break
    finally:
        proc.kill()
        proc.wait()

    clean_output = re.sub(r'\x1b\[[0-9;?]*[a-zA-Z]', '', output)

    passed_pty = "[PTYTEST] UNIX98 PTY/PTS multiplexing & signal isolation PASSED successfully!" in clean_output
    passed_child_sig = "[PASS] Child was terminated by SIGINT from PTY master 0x03!" in clean_output

    print("\n" + "="*50)
    print(f"PTY Signal Isolation: {'PASS' if (passed_pty and passed_child_sig) else 'FAIL'}")
    print("="*50)

    if passed_pty and passed_child_sig:
        print("[SUCCESS] PTY foreground process group signal routing verified!")
        return 0
    else:
        print("[ERROR] Signal isolation test failed.")
        return 1

if __name__ == "__main__":
    sys.exit(run_test())
