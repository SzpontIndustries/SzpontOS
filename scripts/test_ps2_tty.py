#!/usr/bin/env python3
"""Boot the built ISO and test real emulated PS/2 input, plus guest memory regressions.
Build the guest helper first: make -C scripts/tests && make iso
No serial input is used for the TTY checks; QMP send-key drives the PS/2 device.
"""
import argparse
import json
import pathlib
import re
import socket
import shutil
import subprocess
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--usb-image', action='store_true', help='Boot ISO as a USB disk instead of CD-ROM')
p.add_argument('--poll-only', action='store_true', help='Route keyboard IRQ away from IRQ1 to test console polling')
p.add_argument('--throttle', action='store_true', help='Enable QEMU keyboard throttling')
p.add_argument('--machine', default='pc', choices=['pc', 'q35'])
p.add_argument('--firmware', help='OVMF code firmware path (optional)')
p.add_argument('--log', default='/tmp/szpontos-ps2-tty.log')
args = p.parse_args()
with tempfile.TemporaryDirectory(prefix='szpontos-qmp-') as tmp:
    qmp_path = pathlib.Path(tmp)/'qmp.sock'
    log_path = pathlib.Path(args.log)
    cmd = ['qemu-system-x86_64','-M',args.machine,'-cpu','max','-m','512M',
           '-display','none','-cdrom',str(ROOT/'build/szpontos.iso'),
           '-drive',f'file={ROOT}/build/disk.img,format=raw,if=ide,snapshot=on,file.locking=off',
           '-serial','stdio','-qmp',f'unix:{qmp_path},server=on,wait=off','-no-reboot','-no-shutdown']
    if args.usb_image:
        index=cmd.index('-cdrom')
        del cmd[index:index+2]
        cmd += ['-device','qemu-xhci,id=bootxhci',
                '-drive',f'if=none,id=bootusb,format=raw,readonly=on,file={ROOT}/build/szpontos.iso',
                '-device','usb-storage,bus=bootxhci.0,drive=bootusb,bootindex=1']
    if args.poll_only:
        cmd += ['-global','i8042.kbd-irq=7']
    if args.throttle:
        cmd += ['-global','i8042.kbd-throttle=on']
    if args.firmware:
        code=pathlib.Path(args.firmware)
        variables=pathlib.Path(tmp)/'vars.fd'
        shutil.copyfile(code.with_name(code.name.replace('CODE','VARS')),variables)
        cmd += ['-drive',f'if=pflash,format=raw,unit=0,readonly=on,file={code}',
                '-drive',f'if=pflash,format=raw,unit=1,file={variables}']
    with log_path.open('wb') as log:
        proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT)
        try:
            def wait_for(pattern, timeout=30):
                deadline=time.monotonic()+timeout
                while time.monotonic()<deadline:
                    text=log_path.read_text(errors='replace').replace('\r','')
                    text=re.sub(r'\x1b\[[0-9;?]*[a-zA-Z]','',text)
                    if 'CORECHECK_FAIL' in text or 'KERNEL CPU EXCEPTION' in text:
                        raise RuntimeError(text[-4000:])
                    if re.search(pattern,text): return
                    if proc.poll() is not None: raise RuntimeError(text[-4000:])
                    time.sleep(.05)
                raise TimeoutError(f'{pattern}: {text[-4000:]}')
            wait_for("Type 'help'")
            with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as sock:
                sock.connect(str(qmp_path))
                stream=sock.makefile('rwb',buffering=0)
                json.loads(stream.readline())
                def qmp(command, arguments=None):
                    msg={'execute':command}
                    if arguments: msg['arguments']=arguments
                    stream.write(json.dumps(msg).encode()+b'\n')
                    while True:
                        reply=json.loads(stream.readline())
                        if 'error' in reply: raise RuntimeError(reply)
                        if 'return' in reply: return reply['return']
                qmp('qmp_capabilities')
                proc.stdin.write(b'/bin/corecheck\n'); proc.stdin.flush()
                wait_for('TTY_CANON_READY',120)
                def key(*codes):
                    qmp('send-key',{'keys':[{'type':'qcode','data':c} for c in codes],'hold-time':20})
                    time.sleep(.08)
                key('a'); key('shift','b'); key('backspace'); key('c'); key('ret')
                wait_for('TTY_CANON_PASS')
                wait_for('TTY_RAW_READY')
                key('ctrl','c'); key('up')
                wait_for('CORECHECK_PASS')
                print(f'PASS: {args.machine} memory, canonical PS/2 TTY, raw Ctrl+C/Up; log {log_path}')
        finally:
            proc.terminate()
            try: proc.wait(timeout=5)
            except subprocess.TimeoutExpired: proc.kill(); proc.wait()
