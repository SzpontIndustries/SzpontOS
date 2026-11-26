#!/usr/bin/env python3
"""
Test automatyczny renderowania TUI w SzpontOS (tuitest).
"""

import subprocess
import time
import sys
import os
import select
import re

def run_test():
    cmd = [
        "qemu-system-x86_64",
        "-M", "q35",
        "-m", "512M",
        "-smp", "2",
        "-cdrom", "build/szpontos.iso",
        "-boot", "d",
        "-display", "none",
        "-serial", "stdio",
        "-no-reboot",
        "-no-shutdown"
    ]

    print("[TEST] Launching QEMU for TUI Rendering Verification...")
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT
    )

    output = ""
    start_time = time.time()
    test_sent = False
    test_passed = False

    try:
        while time.time() - start_time < 20:
            r, _, _ = select.select([proc.stdout], [], [], 0.2)
            if r:
                try:
                    chunk = os.read(proc.stdout.fileno(), 4096).decode('utf-8', errors='ignore')
                except Exception:
                    break
                if not chunk:
                    if proc.poll() is not None:
                        break
                    time.sleep(0.05)
                    continue

                output += chunk
                sys.stdout.write(chunk)
                sys.stdout.flush()

                clean_chunk = re.sub(r'\x1b\[[0-9;?]*[a-zA-Z]', '', output)

                if not test_sent and "root@szpontos-box:/" in clean_chunk and "Type 'help'" in clean_chunk:
                    time.sleep(0.5)
                    print("\n[TEST] Sending /usr/tbin/tuitest command to shell...")
                    proc.stdin.write(b"/usr/tbin/tuitest\n")
                    proc.stdin.flush()
                    test_sent = True

                if "[SUCCESS] TUI Rendering test completed successfully!" in output:
                    print("\n[PASS] TUI rendering test passed successfully!")
                    test_passed = True
                    time.sleep(0.5)
                    break
            else:
                time.sleep(0.05)

    finally:
        proc.kill()

    if test_passed:
        print("\n==============================================")
        print(">>> ALL TUI & FRAMEBUFFER TESTS PASSED 100%! <<<")
        print("==============================================")
        return True
    else:
        print("\n[FAIL] TUI output verification failed!")
        return False

if __name__ == "__main__":
    if not run_test():
        sys.exit(1)
