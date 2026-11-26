import subprocess
import time
import os
import sys
import select
import pty
import re

def run_test():
    master, slave = pty.openpty()

    qemu_cmd = [
        "./scripts/run_qemu.sh",
        "--headless"
    ]

    proc = subprocess.Popen(
        qemu_cmd,
        stdin=slave,
        stdout=slave,
        stderr=slave,
        close_fds=True
    )
    os.close(slave)

    output = ""
    start_time = time.time()
    sent_command = False
    sent_time = 0

    try:
        while True:
            if sent_command and time.time() - sent_time > 20:
                break
            if not sent_command and time.time() - start_time > 40:
                break

            r, _, _ = select.select([master], [], [], 0.1)
            if r:
                try:
                    raw = os.read(master, 1024)
                    if not raw:
                        break
                    chunk = raw.decode('utf-8', errors='ignore')
                    output += chunk
                    sys.stdout.write(chunk)
                    sys.stdout.flush()

                    if not sent_command and "Type 'help'" in output and "# " in output:
                        time.sleep(2.5)
                        print("\n[TEST] Sending '/usr/tbin/threadtest'...", flush=True)
                        os.write(master, b"/usr/tbin/threadtest\n")
                        sent_command = True
                        sent_time = time.time()

                    if "All POSIX Threads & Synchronization Tests PASSED!" in output:
                        time.sleep(0.5)
                        break
                except OSError:
                    break
            else:
                time.sleep(0.05)
    finally:
        proc.kill()
        os.close(master)

    return output

if __name__ == "__main__":
    out = run_test()
    if "All POSIX Threads & Synchronization Tests PASSED!" in out:
        print("\n[SUCCESS] Thread test verified successfully!")
        sys.exit(0)
    else:
        print("\n[FAILURE] Test did not produce expected success output.")
        sys.exit(1)
