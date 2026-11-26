import subprocess
import time
import os
import sys
import select

def run_test():
    qemu_cmd = [
        "qemu-system-x86_64",
        "-M", "pc",
        "-cpu", "max",
        "-m", "512M",
        "-display", "none",
        "-cdrom", "build/szpontos.iso",
        "-drive", "file=build/disk.img,format=raw,if=ide,index=0,media=disk,snapshot=on,file.locking=off",
        "-serial", "stdio",
        "-no-reboot",
        "-no-shutdown"
    ]

    proc = subprocess.Popen(
        qemu_cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT
    )

    output = ""
    start_time = time.time()
    step = 0
    commands = [
        ("modinfo /lib/modules/hello.sko\n", "Author check"),
        ("modinfo /lib/modules/dummy_dev.sko\n", "Dummy dev info"),
        ("lsmod\n", "Initial lsmod"),
        ("insmod /lib/modules/hello.sko\n", "Insert hello"),
        ("lsmod\n", "Check lsmod with hello"),
        ("cat /proc/modules\n", "Direct procfs check"),
        ("insmod /lib/modules/dummy_dev.sko\n", "Insert dummy_dev"),
        ("lsmod\n", "Check lsmod with both"),
        ("cat /proc/modules\n", "Direct procfs check 2"),
        ("cat /dev/szpont_device\n", "Read dynamic device"),
        ("rmmod hello\n", "Remove hello"),
        ("rmmod dummy_dev\n", "Remove dummy_dev"),
        ("lsmod\n", "Final lsmod")
    ]

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

                if ("root@szpontos" in output or "#" in output) and "Type 'help'" in output:
                    if step < len(commands):
                        cmd, desc = commands[step]
                        time.sleep(0.4)
                        proc.stdin.write(cmd.encode('utf-8'))
                        proc.stdin.flush()
                        step += 1
                    elif step == len(commands):
                        time.sleep(1.0)
                        break
            else:
                time.sleep(0.05)
    finally:
        proc.kill()

    return output

if __name__ == "__main__":
    out = run_test()
    success_markers = [
        "Szpont Industries",
        "insmod: Module '/lib/modules/hello.sko' inserted successfully",
        "insmod: Module '/lib/modules/dummy_dev.sko' inserted successfully",
        "SzpontOS Dynamic Character Device (.sko driver active!)",
        "rmmod: Module 'hello' removed successfully",
        "rmmod: Module 'dummy_dev' removed successfully"
    ]
    all_passed = True
    for marker in success_markers:
        if marker not in out:
            print(f"[ERROR] Missing success marker: '{marker}'")
            all_passed = False

    if all_passed:
        print("\n[SUCCESS] All Kernel Module (.sko) tests PASSED successfully!")
        sys.exit(0)
    else:
        print("\n[FAILURE] Some kernel module tests failed.")
        sys.exit(1)
