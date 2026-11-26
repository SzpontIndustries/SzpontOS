#!/usr/bin/env python3
import subprocess
import time
import os
import sys
import select
import re

def test_timing_and_sleep():
    qemu_cmd = [
        "qemu-system-x86_64",
        "-M", "pc",
        "-cpu", "max",
        "-m", "512M",
        "-display", "none",
        "-cdrom", "build/szpontos.iso",
        "-drive", "file=build/disk.img,format=raw,if=ide,index=0,media=disk,snapshot=on,file.locking=off",
        "-netdev", "user,id=net0",
        "-device", "e1000,netdev=net0",
        "-serial", "stdio",
        "-no-reboot",
        "-no-shutdown"
    ]

    print("[TEST] Launching QEMU to test ping and sleep...")
    proc = subprocess.Popen(
        qemu_cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT
    )

    output = ""
    start_time = time.time()
    state = 0
    sleep_start = 0
    sleep_end = 0

    try:
        while time.time() - start_time < 30:
            r, _, _ = select.select([proc.stdout], [], [], 0.1)
            if r:
                raw = os.read(proc.stdout.fileno(), 1024)
                if not raw:
                    break
                chunk = raw.decode('utf-8', errors='ignore')
                output += chunk
                sys.stdout.write(chunk)
                sys.stdout.flush()

                clean_chunk = re.sub(r'\x1b\[[0-9;?]*[a-zA-Z]', '', output)

                if state == 0 and "root@szpontos-box" in clean_chunk and "Type 'help'" in clean_chunk:
                    time.sleep(0.5)
                    print("\n[TEST] Sending 'ping 127.0.0.1 -c 3'...")
                    proc.stdin.write(b"ping 127.0.0.1 -c 3\n")
                    proc.stdin.flush()
                    state = 1

                elif state == 1 and "--- 127.0.0.1 ping statistics ---" in output:
                    time.sleep(0.5)
                    print("\n[TEST] Sending '/bin/sleep 2'...")
                    sleep_start = time.time()
                    proc.stdin.write(b"/bin/sleep 2\n")
                    proc.stdin.flush()
                    state = 2

                elif state == 2 and clean_chunk.count("root@szpontos-box") >= 3:
                    sleep_end = time.time()
                    elapsed = sleep_end - sleep_start
                    print(f"\n[TEST] /bin/sleep 2 finished in {elapsed:.2f} seconds!")
                    state = 3
                    break
            else:
                time.sleep(0.05)

    finally:
        proc.kill()

    clean_output = re.sub(r'\x1b\[[0-9;?]*[a-zA-Z]', '', output)
    if "rtt min/avg/max" in clean_output and state == 3:
        print("\n[SUCCESS] Microsecond ping RTT measurement and /bin/sleep verified successfully!")
        return 0
    else:
        print("\n[FAILURE] Test did not complete successfully.")
        return 1

if __name__ == "__main__":
    sys.exit(test_timing_and_sleep())
