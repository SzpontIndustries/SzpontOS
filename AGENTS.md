# SzpontOS — Developer & Agent Reference Manual

> **Branding & Copyright:**  
> `(C) Copyright by Szpont Industries. All rights reserved.`  
> **Target Architecture:** `x86_64` (Higher-Half Bare Metal & Ring 3 Userland)  
> **Boot Protocol:** Limine Boot Protocol v8.x (BIOS & UEFI support)

---

## 1. Project Overview

**SzpontOS** is an independent, 64-bit Unix-like operating system written from scratch in C17 and x86_64 Assembly. It features:
- **Higher-Half Monolithic Kernel:** Direct physical mapping via HHDM (`0xFFFF800000000000`) and higher-half execution base (`0xFFFFFFFF80000000`).
- **Symmetric Multiprocessing (SMP):** Native multi-core scheduling (up to 64 CPUs), LAPIC timers, ACPI MADT parsing, and Inter-Processor Interrupts (IPIs).
- **Fast Hardware Syscalls:** Hardware-assisted `syscall` / `sysretq` with per-CPU GS kernel stack switching and full System V AMD64 ABI compliance.
- **Virtual Memory Management:** 4-level x86_64 paging with userland Ring 3 isolation, copy-on-write `fork()`, dynamic heap expansion (`brk()`), and anonymous/file `mmap()`.
- **DRM/KMS Graphics Subsystem & Mesa 3D:** Native Direct Rendering Manager kernel driver (`/dev/dri/card0`, `/dev/dri/renderD128`), PRIME dma-buf sharing, Syncobj explicit GPU handles, dumb buffers, modesetting, and Virtio-VGA acceleration. Full **Mesa 3D (25.x)** Gallium driver stack (`softpipe`, `llvmpipe`, `virtio-gpu`), EGL (`libEGL.so`), OpenGL ES 2.0 (`libGLESv2.so`), and DRI3 zero-copy buffer sharing.
- **Complete X11 Graphical Desktop & Szpont Experience:** Official X.Org X11 server port with modesetting and XKB, **Szpont Experience** super-lightweight desktop environment and reparenting window manager (`szpontdesktop` with hardware-accelerated rounded window borders via `XShape`, glassmorphic TopBar, dynamic taskbar, traffic light controls, cyber titlebars, desktop gradient), `szponterm` terminal emulator with ANSI color and PTY support, and standard X11 libraries (`libX11`, `libxcb`, `libXext`, `libpixman-1`, `libdrm`, `libgbm`).
- **Linux Compatibility & Advanced IPC Subsystems:** Complete `epoll(7)` (Level-Triggered, Edge-Triggered, `EPOLLONESHOT`, `EPOLLEXCLUSIVE`), `kqueue(2)` event notification, `eventfd(2)` 64-bit atomic counter and semaphore, `timerfd(2)` file-descriptor-driven timers, `signalfd(4)` asynchronous signal capture, `inotify(7)` filesystem watch subsystem integrated into VFS, virtual `sysfs` (`/sys`), atomic `pipe2(2)` and `dup3(2)`, directory descriptor navigation via `fchdir(2)`, and POSIX process group / session isolation (`setsid`, `setpgid`, `TIOCSPGRP`).
- **Freestanding C Standard Library & C++ Runtime:** LP64 POSIX C library (`libc.a`, `libc.so`, `libm.so`, `libdl.a`), full C++ runtime (**GNU libstdc++-v3**: `libstdc++.so`, `libstdc++.a`) with RTTI and exception handling.
- **In-Kernel Dynamic ELF Loader:** Automatic `DT_NEEDED` dependency resolution, shared object mapping at `0x0000700000000000`, System V AMD64 auxiliary vector (`auxv`), and runtime ELF relocations.
- **Modular VFS & Storage:** Virtual File System with DevFS, ProcFS, SysFS, TmpFS, Ext2 filesystem driver, Buffer Cache (`bcache` with 256 hash buckets and doubly-linked LRU eviction), and USTAR Initramfs.
- **Networking Stack & Daemons:** BSD sockets (TCP/IP, UDP, ICMP, ARP, DHCP, DNS), Intel E1000 and Realtek RTL8139 NIC drivers, native OpenSSH suite (`sshd`, `ssh`, `ssh-keygen`), OpenSSL/LibreSSL, cURL (`libcurl`), Git, and embedded HTTP server (`httpd`).
- **Hardware & Input Subsystem:** xHCI (USB 3.0) and EHCI (USB 2.0) controllers with USB HID keyboard/mouse driver, i8042 PS/2 controller, evdev subsystem (`/dev/input/event*`, `/dev/input/mice`), CMOS RTC, and TSC precision timing.

---

## 2. Directory Structure

```
SzpontOS/
├── kernel/                      # Higher-Half Operating System Kernel
│   ├── arch/x86_64/             # Architecture-specific CPU initialization
│   │   ├── gdt.c / idt.c        # GDT, TSS, IDT, 256 interrupt gates
│   │   ├── pic.c / pit.c        # 8259 PIC remapping, PIT 100 Hz timer
│   │   ├── mtrr.c               # MTRR Write-Combining setup for VRAM
│   │   ├── isr.asm              # Interrupt Service Routines & CPU exception handlers
│   │   ├── context.asm          # arch_switch_context & arch_enter_user_mode (iretq)
│   │   ├── syscall_arch.c       # Fast syscall MSR initialization (EFER.SCE, STAR, LSTAR)
│   │   └── syscall_entry.asm    # Syscall hardware entry, GS stack switch & sysret
│   ├── include/                 # Kernel internal headers
│   │   ├── arch/x86_64/         # GDT, IDT, PIC, PIT, IO port primitives, CPU registers
│   │   ├── drivers/             # DRM, FB console, UART, keyboard, RTC, ATA, AHCI, E1000, PCI, IOAPIC, ACPI, PTY, Evdev
│   │   ├── fs/                  # VFS, DevFS, ProcFS, SysFS, Ext2, TmpFS, Buffer Cache, Initramfs, ELF-64, Epoll, Eventfd, Inotify
│   │   ├── mm/                  # PMM (bitmap), VMM (4-level paging), Heap (slab/buddy), Usercopy
│   │   ├── mod/                 # Loadable Kernel Modules (.sko) subsystem
│   │   ├── net/                 # Ethernet, ARP, IPv4, ICMP, UDP, TCP, Netdev, Sockets
│   │   ├── sched/               # process_t, thread_t, futex, waitqueues, SMP scheduler
│   │   ├── syscall/             # POSIX syscall dispatcher and prototypes
│   │   ├── kernel/              # Types, kprint, panic, spinlocks, SMP (cpu_t), string utilities
│   │   ├── uapi/                # Linux User-Space API headers exported to sysroot (asm/, linux/)
│   │   └── limine.h             # Limine bootloader protocol specification
│   ├── src/                     # Kernel core implementation
│   │   ├── main.c               # Kernel entry point (_start) and subsystem initialization
│   │   ├── string.c / kprint.c  # Kernel string library, kprintf, ksnprintf, klog
│   │   ├── panic.c              # Kernel panic handler and register dump
│   │   ├── kernel/              # smp.c (multi-core bootstrap, IPI, LAPIC timer), kqueue.c
│   │   ├── drivers/             # drm.c, framebuffer.c, serial.c, keyboard.c, mouse.c, ps2_mouse.c,
│   │   │                        # rtc.c, ata.c, ahci.c, pci.c, e1000.c, rtl8139.c, ioapic.c, acpi.c,
│   │   │                        # pty.c, evdev.c, random.c, power.c, usb/ (ehci.c, xhci.c, hid.c)
│   │   ├── fs/                  # vfs.c, devfs.c, procfs.c, sysfs.c, tmpfs.c, ext2.c, bcache.c, initramfs.c,
│   │   │                        # elf.c, epoll.c, eventfd.c, inotify.c, timerfd.c, signalfd.c
│   │   ├── mm/                  # pmm.c, vmm.c, heap.c, usercopy.c
│   │   ├── mod/                 # module.c (ELF module loader, symbol resolution, relocations)
│   │   ├── net/                 # netif.c, ethernet.c, arp.c, ipv4.c, icmp.c, udp.c, tcp.c, socket.c
│   │   ├── sched/               # process.c, sched.c, futex.c, waitqueue.c
│   │   └── syscall/             # syscall.c (120+ POSIX / Linux system call handlers)
│   └── linker.ld                # Higher-half linker script (0xFFFFFFFF80000000)
│
├── libc/                        # Freestanding C Standard Library (builds libc.a, libc.so, libm.so, libdl.a)
│   ├── include/                 # Standard POSIX headers (100% clean POSIX/C17/BSD)
│   │   ├── stdio.h, stdlib.h, string.h, unistd.h, fcntl.h, dirent.h, errno.h, time.h
│   │   ├── pthread.h, semaphore.h, dlfcn.h, math.h, termios.h, poll.h, signal.h
│   │   ├── stdint.h, stddef.h, stdbool.h, stdarg.h (LP64 self-contained headers)
│   │   └── sys/ (stat.h, types.h, socket.h, mman.h, poll.h, utsname.h, wait.h, time.h, ioctl.h, shm.h, epoll.h, eventfd.h, inotify.h)
│   └── src/
│       ├── arch/x86_64/         # crt0.asm, syscall.asm, setjmp.asm
│       ├── stdio/               # printf.c, snprintf.c, puts, putchar, getchar, file ops
│       ├── stdlib/              # malloc.c (sbrk/mmap heap allocator), strtol, atoi, env
│       ├── string/              # Standard string and memory manipulation routines
│       ├── time/                # time.c (clock_gettime, gettimeofday, time, nanosleep), timerfd.c
│       ├── signal/              # signalfd.c, sigaction, sigprocmask
│       ├── poll/                # poll.c, epoll.c
│       ├── pthread/             # POSIX threads, mutexes, condvars, barriers, semaphores, TLS
│       ├── socket/              # Berkeley Sockets API (socket, connect, bind, listen, recv, send, SCM_RIGHTS)
│       ├── dlfcn/               # Dynamic linker routines (dlopen, dlsym, dlclose, dlerror)
│       ├── netdb/               # getaddrinfo, gethostbyname, DNS resolver
│       └── unistd/              # POSIX syscall wrappers (fork, execve, read, write, sleep, fchdir, etc.)
│
├── third_party/                 # Ported open-source packages cross-compiled against libc sysroot
│   ├── libdrm/                  # Official Linux libdrm submodule (libdrm.so, libdrm_intel, libdrm_amdgpu, libdrm_nouveau, libdrm_radeon)
│   ├── xorg/                    # Official X.Org X11 Server (Xorg binary with native DRM/KMS modesetting)
│   ├── mesa/                    # Mesa 3D (25.x): Gallium drivers, EGL, OpenGL ES 2.0, DRI3
│   ├── libX11/, libxcb/, ...    # Core X11 client libraries (libX11, libXext, libXau, libXdmcp, libxkbfile, etc.)
│   ├── pixman/                  # Low-level pixel manipulation library (libpixman-1.so)
│   ├── libstdc++/               # GNU C++ Standard Library runtime (libstdc++.so, libstdc++.a)
│   ├── openssh/                 # OpenSSH client and daemon (sshd, ssh, ssh-keygen)
│   ├── curl/                    # Libcurl & curl command-line utility
│   ├── zlib/                    # Compression library (libz.so)
│   ├── ncurses/                 # GNU Ncurses (libncurses.a)
│   ├── nano/                    # GNU nano editor (/bin/nano)
│   ├── zsh/                     # Z shell (/bin/zsh)
│   ├── file/                    # Libmagic & GNU file (/bin/file)
│   └── fastfetch/               # System information tool (/bin/fastfetch)
│
├── userland/                    # User space programs and root filesystem
│   ├── init/main.c              # PID 1 init process (spawns /bin/sh or graphical session)
│   ├── sh/main.c                # Interactive Unix shell with built-ins & history
│   ├── bin/                     # Essential /bin utilities (cat, chmod, chown, cp, date, df, dmesg, echo,
│   │                            # kill, ln, ls, mkdir, mount, mv, ps, rm, rmdir, sh, sleep, sync, umount, uname)
│   │                            # and extended /usr/bin utilities (szpontdesktop, szponterm, szpontlogin,
│   │                            # startx, top, nano, zsh, git, curl, openssl, ssh, fastfetch, donut, etc.)
│   └── skeleton/                # Static rootfs skeleton templates (/etc/passwd, /etc/magic, /etc/ssh, etc.)
│
├── mk/                          # Modular Build System Makefiles
│   ├── third_party.mk           # Third-party packages, shared libraries & Autotools cross-build rules
│   ├── iso.mk                   # USTAR initramfs packaging and bootable Limine ISO generation
│   └── qemu.mk                  # Emulator execution profiles (virtio, ps2, usb, stress, cli, debug)
│
├── scripts/                     # Toolchain & execution helper scripts
│   ├── run_qemu.sh              # Unified QEMU launcher with port forwarding and hardware toggles
│   ├── make_initramfs.py        # Python script packaging rootfs into USTAR initramfs.tar
│   ├── make_ext2_disk.py        # Disk image generator for secondary block storage
│   ├── build_libstdcxx.py       # Standalone C++ runtime builder
│   └── generate_ncurses_fallbacks.py
│
├── limine.conf                  # Limine bootloader boot configuration
├── config.mk                    # Toolchain detection, compiler flags, and sysroot paths
├── Makefile                     # Root orchestration Makefile with parallel DAG dependency rules
├── .clangd                      # Clangd language server bare-metal configuration
└── .vscode/c_cpp_properties.json# IDE include paths and IntelliSense configuration
```

---

## 3. Core Technical Principles & Architecture

### 3.1 Memory Layout (x86_64 4-Level Paging)
- **Higher-Half Kernel Base:** `0xFFFFFFFF80000000` (mapped via linker script and Limine).
- **HHDM (Higher-Half Direct Map Base):** `0xFFFF800000000000` (physical memory offset `g_hhdm_base`).
- **Userland Virtual Address Space:** `0x0000000000400000` – `0x00007FFFFFFFFFFF` (Ring 3, DPL=3).
- **Shared Object (.so) Load Base:** Starts at `0x0000700000000000` (dynamically assigned per library).
- **User Stack Base:** `0x00007FFFF0000000` (grows down, default size 4 MiB).
- **User Heap (`brk`):** Starts at `0x0000000000800000` and dynamically expands via `sys_brk`.

### 3.2 Physical Address Masking Rule
> [!IMPORTANT]
> When traversing or modifying 64-bit page table entries (PML4, PDPT, PD, PT), **always** mask using `PHYS_ADDR_MASK` (`0x000FFFFFFFFFF000ULL`).
> Never use `~0xFFFULL`, as bit 63 (`NX` / No Execute) or other high architectural bits will corrupt the physical pointer converted via `PHYS_TO_VIRT()`.

### 3.3 Hardware Interrupts, PIC EOI & Idle Loop
1. **Interrupt Handler then EOI Ordering:** In [kernel/arch/x86_64/idt.c](kernel/arch/x86_64/idt.c), registered interrupt handlers are executed **before** sending EOI (PIC `outb(0x20, 0x20)` or LAPIC EOI). This ensures hardware device handlers (e.g. keyboard port `0x60`) can read data before the next interrupt is allowed. On bare metal hardware, sending EOI before the handler causes scancode loss due to real IO-APIC/LAPIC timing.
2. **Idle Thread CPU Halt:** In [kernel/src/sched/sched.c](kernel/src/sched/sched.c), `g_idle_thread` executes `__asm__ volatile ("sti; hlt; cli");`. This ensures the CPU halts in low-power state with interrupts enabled so timer ticks can wake it.
3. **Blocking vs Spinning:** Never spin in a tight `sched_yield()` loop in syscalls. For blocking operations (e.g. `waitpid`), use `thread_sleep(10)` or waitqueues so the scheduler can yield to `idle_thread` and allow timer ticks to advance.

### 3.4 Hardware RTC & CPU TSC Precision Timing
- **CMOS RTC:** [kernel/src/drivers/rtc.c](kernel/src/drivers/rtc.c) reads ports `0x70`/`0x71` with Update-In-Progress (`UIP`) polling and BCD conversion to Unix epoch.
- **TSC Calibration:** At boot, TSC frequency is calibrated against PIT hardware counter latch (port `0x40`) without requiring interrupts (`g_tsc_freq_hz`).
- **Monotonic High-Precision Time:** `rtc_get_monotonic()` / `sys_clock_gettime(CLOCK_MONOTONIC)` computes time directly from `rdtsc()`, providing sub-microsecond precision (used by `ping`, `sleep`, profiling).

### 3.5 Networking Stack & NIC Driver (Intel 8254x / E1000)
- The network stack supports Ethernet, ARP, IPv4, ICMP, UDP, and TCP (state machine with 3-way handshake).
- **Packet Polling Rule:** During socket reads (`recvfrom`, `read`) and `poll`, `e1000_poll()` is called immediately to process incoming frames from SLIRP without waiting for the PIT/LAPIC timer tick.

### 3.6 Ring 3 Transition & Fast Syscalls
- **Switching to User Mode:** Handled in [kernel/arch/x86_64/context.asm](kernel/arch/x86_64/context.asm) (`arch_enter_user_mode`) by setting selectors `CS=0x23`, `DS/ES/FS/GS=0x1B`, `SS=0x1B`, `RFLAGS=0x202`, and executing `iretq`.
- **Fast Syscalls:** Handled via `syscall` / `sysretq`. Hardware jumps to `syscall_entry` in [kernel/arch/x86_64/syscall_entry.asm](kernel/arch/x86_64/syscall_entry.asm), saves user RSP, switches to `g_current_kernel_stack` (via `%gs:16`), and maps arguments according to System V AMD64 ABI:
  - `RAX` (sys_no) $\rightarrow$ `RDI`
  - `RDI` (arg1) $\rightarrow$ `RSI`
  - `RSI` (arg2) $\rightarrow$ `RDX`
  - `RDX` (arg3) $\rightarrow$ `RCX`
  - `R10` (arg4) $\rightarrow$ `R8`
  - `R8`  (arg5) $\rightarrow$ `R9`

### 3.7 Symmetric Multiprocessing (SMP) & Per-CPU GS Layout
> [!CAUTION]
> **CRITICAL ASSEMBLY STRUCT OFFSETS:**  
> The per-CPU structure `cpu_t` defined in [kernel/include/kernel/smp.h](kernel/include/kernel/smp.h) is accessed directly by assembly routines in [syscall_entry.asm](kernel/arch/x86_64/syscall_entry.asm) via the `%gs` segment base (`%gs:0`).  
> **Never insert, reorder, or resize fields** in `cpu_t` without synchronizing the exact byte offsets in `syscall_entry.asm` and `context.asm`:
> - `Offset 0x00 (0)`: `self` (pointer to this `cpu_t`)
> - `Offset 0x08 (8)`: `cpu_id` (`uint32_t`)
> - `Offset 0x0C (12)`: `lapic_id` (`uint32_t`)
> - `Offset 0x10 (16)`: `kernel_stack` (`uintptr_t`, loaded into `%rsp` during syscall)
> - `Offset 0x18 (24)`: `user_rsp` (`uintptr_t`, saved user `%rsp` during syscall)
> - `Offset 0x20 (32)`: `current_thread` (`struct thread *`)
> - `Offset 0x28 (40)`: `idle_thread` (`struct thread *`)
> - `Offset 0x30 (48)`: `boot_rsp` (`uintptr_t`)
> - `Offset 0x38 (56)`: `online` (`volatile uint8_t`)
> - `Offset 0x39 (57)`: `is_bsp` (`uint8_t`)

- Multi-core initialization is handled via Limine's SMP boot protocol in [kernel/src/kernel/smp.c](kernel/src/kernel/smp.c).
- Each CPU core configures its own TSS, GDT, LAPIC timer (1000 Hz), and `%gs` base via `wrmsr(MSR_GS_BASE)`.
- Inter-processor coordination uses IPIs (`smp_send_ipi`) and spinlocks.

### 3.8 Safe Ring 3 Memory Access (`usercopy`)
> [!IMPORTANT]
> **NEVER DIRECTLY DEREFERENCE USERSPACE POINTERS IN RING 0!**  
> Direct dereferencing of userland pointers in kernel mode causes unhandled page faults or critical security vulnerabilities.
> Always use the checked copy functions from [kernel/include/mm/usercopy.h](kernel/include/mm/usercopy.h):
> - `bool copy_from_user(void *dst, uintptr_t user_src, size_t len)`
> - `bool copy_to_user(uintptr_t user_dst, const void *src, size_t len)`
> - `char *copy_string_from_user(uintptr_t user_src, size_t max_len)`
>
> These functions validate canonical userland boundaries (`USER_ADDR_MAX = 0x00007FFFFFFFFFFFULL`), walk the process page table, and copy bytes through the kernel's HHDM direct physical mapping. Unmapped or non-writable pages return `false` / `-EFAULT` cleanly.

### 3.9 In-Kernel Dynamic ELF Loader & Shared Library Resolution
- Unlike traditional Linux where `ld-linux.so` runs in userland, SzpontOS incorporates an in-kernel dynamic ELF loader in [kernel/src/fs/elf.c](kernel/src/fs/elf.c).
- When executing a dynamically linked binary:
  1. Segments (`PT_LOAD`) are mapped into user memory with appropriate permissions (`PF_R`, `PF_W`, `PF_X`).
  2. The dynamic section (`PT_DYNAMIC`) is inspected for `DT_NEEDED` dependencies.
  3. Shared objects (`.so`) are automatically located in `/lib` and `/usr/lib`, mapped into virtual memory starting from `SO_BASE_START = 0x0000700000000000ULL`.
  4. Dynamic symbol tables (`DT_SYMTAB`, `DT_STRTAB`, `DT_HASH`) are resolved and relocations applied (`R_X86_64_RELATIVE`, `R_X86_64_GLOB_DAT`, `R_X86_64_JUMP_SLOT`, `R_X86_64_64`).
- Userland binaries also have access to `dlopen()`, `dlsym()`, `dlclose()`, and `dlerror()` through `libdl.a` / `libc/src/dlfcn/dlfcn.c`.

### 3.10 DRM/KMS Graphics Subsystem & Buffer Management
- **Device Nodes:**
  - `/dev/dri/card0`: Primary DRM node with master PID authentication, mode enumeration, CRTC/connector configuration, and dumb buffer allocation.
  - `/dev/dri/renderD128`: Render node allowing unprivileged 3D/compute access and buffer sharing without modesetting privileges.
- **Dumb Buffers & Framebuffers:** Allocated via `DRM_IOCTL_MODE_CREATE_DUMB` and wrapped in FB IDs via `DRM_IOCTL_MODE_ADDFB`.
- **PRIME dma-buf Sharing:** Kernel supports exporting buffer objects to file descriptors (`DRM_IOCTL_PRIME_HANDLE_TO_FD`) and importing them (`DRM_IOCTL_PRIME_FD_TO_HANDLE`), enabling zero-copy buffer sharing across processes.
- **Sync Objects:** Explicit GPU synchronization primitives (`drm_syncobj_t`) supporting create, destroy, signal, and wait ioctls.
- **X.Org & Desktop Integration:** The official X11 server connects to `/dev/dri/card0` using standard modesetting and `libdrm.so`.

### 3.11 IPC, Sockets & File Descriptor Passing (`SCM_RIGHTS`)
- UNIX domain sockets (`AF_UNIX`) fully support ancillary control messages (`sendmsg` / `recvmsg` with `SCM_RIGHTS`).
- This allows transferring open file descriptors (including PRIME dma-buf graphics buffers) between the X11 server and client applications (e.g. DRI3 protocol).
- SysV Shared Memory (`SYS_shmget`, `SYS_shmat`, `SYS_shmctl`, `SYS_shmdt`) is implemented for MIT-SHM high-speed graphics blitting.
- PTY subsystem (`/dev/ptmx` and `/dev/pts/N`) provides full pseudoterminal master/slave multiplexing for terminal emulators like `szponterm`.

### 3.12 Input Pipeline: Evdev, PS/2 & USB HID
- Drivers for i8042 PS/2 controller (`ps2_mouse.c`, `keyboard.c`) and USB HID (`usb/hid.c` via `xhci.c` and `ehci.c`) feed directly into the unified evdev subsystem in [kernel/src/drivers/evdev.c](kernel/src/drivers/evdev.c).
- Exposes:
  - `/dev/input/mice`: Emulates standard 3-byte / 4-byte Explorer PS/2 packets for legacy X11 mouse drivers.
  - `/dev/input/event0` .. `/dev/input/eventN`: Linux-compatible `struct input_event` streams for modern event handling.

### 3.13 Linux Compatibility Event Subsystems (`epoll`, `timerfd`, `signalfd`, `eventfd`, `inotify`)
- **epoll(7):** Implemented in [kernel/src/fs/epoll.c](kernel/src/fs/epoll.c). Maintains interest list of monitored file descriptors with `epitem` structures. Supports `EPOLL_CTL_ADD`, `EPOLL_CTL_MOD`, `EPOLL_CTL_DEL`, Edge-Triggered (`EPOLLET`), Level-Triggered (default), `EPOLLONESHOT`, and `EPOLLEXCLUSIVE`. Directly integrated with VFS poll callbacks (`vfs_poll()`).
- **eventfd(2):** Implemented in [kernel/src/fs/eventfd.c](kernel/src/fs/eventfd.c). In-kernel 64-bit unsigned integer counter used as an event wait/notify mechanism. Supports `EFD_SEMAPHORE` (reads decrement counter by 1) and `EFD_NONBLOCK`.
- **timerfd(2):** Implemented in [kernel/src/fs/timerfd.c](kernel/src/fs/timerfd.c). Delivers timer expiration notifications via file descriptors. Configured via `timerfd_settime` for one-shot or periodic intervals; reads return the number of expirations since the last read.
- **signalfd(4):** Implemented in [kernel/src/fs/signalfd.c](kernel/src/fs/signalfd.c). Accepts signals synchronous with application event loops via file descriptors without requiring asynchronous signal handlers, returning structured `struct signalfd_siginfo`.
- **inotify(7):** Implemented in [kernel/src/fs/inotify.c](kernel/src/fs/inotify.c). Integrated directly into VFS entry points (`vfs_notify`), queueing filesystem events (`IN_CREATE`, `IN_DELETE`, `IN_MODIFY`, `IN_ATTRIB`, `IN_MOVE`) on watched directory and file nodes.

### 3.14 Process Group Isolation & TTY Foreground Signal Routing (`TIOCSPGRP`, `TIOCGPGRP`)
- **POSIX Sessions & Process Groups:** Implemented via `setsid()`, `setpgid()`, `getpgrp()`, and `getpgid()`.
- **Controlling Terminal & Signal Routing Invariant:**
  - In [kernel/src/drivers/pty.c](kernel/src/drivers/pty.c) and TTY drivers, keyboard-generated interrupt signals (`SIGINT` on Ctrl+C, `SIGQUIT` on Ctrl+\, `SIGTSTP` on Ctrl+Z) **must only be dispatched to the active foreground process group** (`tty->pgrp`), configured via `ioctl(fd, TIOCSPGRP, &pgrp)`.
  - Never broadcast keyboard signals to the entire session or background processes. This prevents background services (e.g., X11 server, window manager, init, network daemons) from dying when Ctrl+C is pressed in an interactive shell.

### 3.15 Mesa 3D, Gallium, DRI3 & Hardware-Accelerated Graphics Architecture
- **Mesa 3D Integration:** Official Mesa 3D (25.x) port cross-compiled against SzpontOS libc and UAPI sysroot headers.
- **Drivers & Libraries:** Gallium architectural drivers (`softpipe`, `llvmpipe`, `virtio-gpu`), EGL (`libEGL.so`), OpenGL ES 2.0 (`libGLESv2.so`), and core Mesa gallium runtime (`libgallium-25.0.5.so`).
- **DRI3 Protocol & PRIME dma-buf:** Zero-copy buffer exchange between X11 clients and the Xorg server is achieved using PRIME dma-buf file descriptors (`DRM_IOCTL_PRIME_HANDLE_TO_FD` / `DRM_IOCTL_PRIME_FD_TO_HANDLE`) passed through UNIX domain sockets using `sendmsg`/`recvmsg` with `SCM_RIGHTS`.
- **Generic Buffer Management (GBM):** Native `libgbm.so` provides buffer allocation and surface creation for KMS modesetting without X11.

### 3.16 Buffer Cache (`bcache`) with 256 Hash Buckets & LRU Eviction
- Located in [kernel/src/fs/bcache.c](kernel/src/fs/bcache.c), the buffer cache caches underlying block storage (Ext2, SATA/AHCI, IDE) in 1024-byte block units.
- **Hash Table:** 256 hash buckets keyed by `(device_id, block_no)` provide $O(1)$ block lookup.
- **Eviction Policy:** Doubly-linked Least Recently Used (LRU) list with reference counting. Dirty buffers are written back to storage on `bcache_sync()`, invoked by `sync(2)`, `fsync(2)`, and during clean system shutdown.

### 3.17 System V AMD64 Auxiliary Vector (`auxv`) & Execution Context
- In [kernel/src/fs/elf.c](kernel/src/fs/elf.c), when spawning ELF binaries via `execve()`, the kernel sets up the initial process user stack conforming to the System V AMD64 ABI:
  1. `argc` (`uint64_t`)
  2. `argv[0] ... argv[argc-1]`, `NULL`
  3. `envp[0] ... envp[N]`, `NULL`
  4. Auxiliary vector entries `Elf64_auxv_t[]` terminated by `AT_NULL (0)`
- **Supplied Vectors:** `AT_PHDR`, `AT_PHENT`, `AT_PHNUM`, `AT_PAGESZ` (4096), `AT_BASE` (0 for static or shared object load base), `AT_FLAGS`, `AT_ENTRY`, `AT_UID`, `AT_EUID`, `AT_GID`, `AT_EGID`, `AT_CLKTCK` (1000 Hz), `AT_RANDOM` (16 bytes of CSPRNG entropy).

### 3.18 Virtual Device Filesystem (`SysFS`)
- Mounted at `/sys` ([kernel/src/fs/sysfs.c](kernel/src/fs/sysfs.c)), SysFS exposes standard Linux-compatible device hierarchies:
  - `/sys/bus/pci/devices/`: Exposes connected PCI devices with device/vendor attributes and resource memory maps.
  - `/sys/class/drm/`: Exposes graphics card (`card0`) and render node (`renderD128`) presence and state.

---

## 4. Syscall Reference Table

The kernel implements over 100 POSIX system calls in [kernel/src/syscall/syscall.c](kernel/src/syscall/syscall.c):

| Syscall # | Name | Description |
|---|---|---|
| 0 | `SYS_read` | Read data from file descriptor |
| 1 | `SYS_write` | Write data to file descriptor |
| 2 | `SYS_open` | Open file or device |
| 3 | `SYS_close` | Close file descriptor |
| 4 | `SYS_stat` | Retrieve file status by path |
| 5 | `SYS_fstat` | Retrieve file status by descriptor |
| 6 | `SYS_lstat` | Retrieve symbolic link status |
| 7 | `SYS_poll` | Wait for I/O events on file descriptors |
| 8 | `SYS_lseek` | Reposition read/write file offset |
| 9 | `SYS_mmap` | Map pages into process address space |
| 10 | `SYS_mprotect` | Set protection on memory region |
| 11 | `SYS_munmap` | Unmap pages from process address space |
| 12 | `SYS_brk` | Expand or contract process heap |
| 13 | `SYS_rt_sigaction` | Examine and change signal action |
| 14 | `SYS_rt_sigprocmask` | Change list of blocked signals |
| 16 | `SYS_ioctl` | Device control operations (DRM, TTY, FB, sockets) |
| 17 | `SYS_pread64` | Read from file offset without changing file pointer |
| 18 | `SYS_pwrite64` | Write to file offset without changing file pointer |
| 19 | `SYS_readv` | Read data into multiple buffers (scatter) |
| 20 | `SYS_writev` | Write data from multiple buffers (gather) |
| 21 | `SYS_access` | Check user permissions for file |
| 22 | `SYS_pipe` | Create unidirectional IPC data channel |
| 23 | `SYS_select` | Synchronous I/O multiplexing |
| 24 | `SYS_yield` | Yield remaining CPU timeslice |
| 28 | `SYS_madvise` | Memory usage hints (`MADV_WILLNEED`, `MADV_FREE`, ...); `MADV_DONTNEED` returns `-EINVAL` |
| 29 | `SYS_shmget` | Allocate SysV shared memory segment |
| 30 | `SYS_shmat` | Attach SysV shared memory segment |
| 31 | `SYS_shmctl` | Control SysV shared memory segment |
| 32 | `SYS_dup` | Duplicate open file descriptor |
| 33 | `SYS_dup2` | Duplicate file descriptor to target index |
| 34 | `SYS_pause` | Wait for signal |
| 35 | `SYS_nanosleep` | High-precision thread sleep |
| 37 | `SYS_alarm` | Set alarm clock for delivery of signal |
| 39 | `SYS_getpid` | Get process ID of calling process |
| 41 | `SYS_socket` | Create communication endpoint |
| 42 | `SYS_connect` | Initiate connection on socket |
| 43 | `SYS_accept` | Accept connection on socket |
| 44 | `SYS_sendto` | Send message on socket |
| 45 | `SYS_recvfrom` | Receive message from socket |
| 46 | `SYS_sendmsg` | Send message with ancillary data (`SCM_RIGHTS`) |
| 47 | `SYS_recvmsg` | Receive message with ancillary data (`SCM_RIGHTS`) |
| 48 | `SYS_shutdown` | Shut down part of a full-duplex socket connection |
| 49 | `SYS_bind` | Bind socket to local address |
| 50 | `SYS_listen` | Listen for incoming socket connections |
| 51 | `SYS_getsockname` | Retrieve current address of socket |
| 52 | `SYS_getpeername` | Retrieve peer address of connected socket |
| 53 | `SYS_socketpair` | Create pair of connected UNIX sockets |
| 54 | `SYS_setsockopt` | Set options on socket |
| 55 | `SYS_getsockopt` | Retrieve options from socket |
| 56 | `SYS_clone` | Create thread/process (`CLONE_VM`, `CLONE_THREAD`, `CLONE_FS`) |
| 57 | `SYS_fork` | Fork child process with COW address space clone |
| 59 | `SYS_execve` | Execute ELF binary with arguments and environment |
| 60 | `SYS_exit` | Terminate calling thread / process |
| 61 | `SYS_wait4` | Wait for state changes in child processes |
| 62 | `SYS_kill` | Send signal to process |
| 63 | `SYS_uname` | Retrieve system name and OS identification |
| 67 | `SYS_shmdt` | Detach SysV shared memory segment |
| 72 | `SYS_fcntl` | Manipulate file descriptor properties |
| 76 | `SYS_truncate` | Truncate file to specified length by path |
| 77 | `SYS_ftruncate` | Truncate file to specified length by descriptor |
| 78 | `SYS_getdents` | Read directory entries into buffer |
| 79 | `SYS_getcwd` | Get current working directory pathname |
| 80 | `SYS_chdir` | Change current working directory |
| 81 | `SYS_fchdir` | Change current working directory by descriptor |
| 82 | `SYS_rename` | Change name or location of file |
| 83 | `SYS_mkdir` | Create directory |
| 84 | `SYS_rmdir` | Remove directory |
| 85 | `SYS_creat` | Create and open new file |
| 86 | `SYS_link` | Create hard link to file |
| 87 | `SYS_unlink` | Remove directory entry / delete file |
| 88 | `SYS_symlink` | Create symbolic link |
| 89 | `SYS_readlink` | Read target value of symbolic link |
| 90 | `SYS_chmod` | Change file permissions by path |
| 91 | `SYS_fchmod` | Change file permissions by descriptor |
| 92 | `SYS_chown` | Change file ownership by path |
| 93 | `SYS_fchown` | Change file ownership by descriptor |
| 95 | `SYS_umask` | Set file mode creation mask |
| 96 | `SYS_gettimeofday` | Get time with microsecond resolution |
| 97 | `SYS_getrlimit` | Get process resource limits |
| 98 | `SYS_getrusage` | Get process resource utilization |
| 99 | `SYS_sysinfo` | Retrieve system statistics (memory, uptime, load) |
| 100 | `SYS_times` | Get process execution times |
| 101 | `SYS_sleep` | Sleep for specified seconds |
| 102 | `SYS_getuid` | Get real user ID |
| 103 | `SYS_syslog` | Read or control kernel message ring buffer |
| 104 | `SYS_getgid` | Get real group ID |
| 105 | `SYS_setuid` | Set real/effective user ID |
| 106 | `SYS_setgid` | Set real/effective group ID |
| 107 | `SYS_geteuid` | Get effective user ID |
| 108 | `SYS_getegid` | Get effective group ID |
| 109 | `SYS_setpgid` | Set process group ID |
| 110 | `SYS_getppid` | Get parent process ID |
| 111 | `SYS_getpgrp` | Get process group ID |
| 112 | `SYS_setsid` | Create session and set process group ID |
| 113 | `SYS_setreuid` | Set real and effective user IDs |
| 114 | `SYS_setregid` | Set real and effective group IDs |
| 115 | `SYS_getgroups` | Get list of supplementary group IDs |
| 116 | `SYS_setgroups` | Set list of supplementary group IDs |
| 117 | `SYS_setresuid` | Set real, effective, and saved user IDs |
| 118 | `SYS_getresuid` | Get real, effective, and saved user IDs |
| 119 | `SYS_setresgid` | Set real, effective, and saved group IDs |
| 120 | `SYS_getresgid` | Get real, effective, and saved group IDs |
| 121 | `SYS_getpgid` | Get process group ID of process |
| 124 | `SYS_getsid` | Get session ID of process |
| 125 | `SYS_seteuid` | Set effective user ID |
| 126 | `SYS_setegid` | Set effective group ID |
| 127 | `SYS_rt_sigpending` | Examine pending signals |
| 137 | `SYS_statfs` | Get filesystem statistics |
| 138 | `SYS_fstatfs` | Get filesystem statistics by descriptor |
| 140 | `SYS_getpriority` | Get program scheduling priority |
| 141 | `SYS_setpriority` | Set program scheduling priority |
| 156 | `SYS_sysctl` | Read or write system control parameters |
| 157 | `SYS_prctl` | Thread name (`PR_SET_NAME` / `PR_GET_NAME`) and `PR_SET_NO_NEW_PRIVS` / `PR_GET_NO_NEW_PRIVS` |
| 158 | `SYS_arch_prctl` | Set architecture-specific thread state (`FS_BASE` / `GS_BASE`) |
| 160 | `SYS_setrlimit` | Set process resource limits |
| 162 | `SYS_sync` | Synchronize cached filesystem buffers to disk |
| 169 | `SYS_reboot` | Reboot or power off system |
| 172 | `SYS_iopl` | Change I/O privilege level |
| 173 | `SYS_ioperm` | Set port I/O permissions |
| 175 | `SYS_init_module` | Load kernel module (`.sko`) |
| 176 | `SYS_delete_module`| Unload kernel module |
| 178 | `SYS_getprocs` | Retrieve process table snapshot |
| 186 | `SYS_gettid` | Get thread ID |
| 200 | `SYS_tkill` | Send signal to specific thread |
| 201 | `SYS_time` | Get current Unix epoch timestamp (seconds) |
| 202 | `SYS_futex` | Fast user-space locking (`FUTEX_WAIT`, `FUTEX_WAKE`) |
| 204 | `SYS_sched_getaffinity` | Report the CPU mask (all online CPUs; threads are not pinned) |
| 218 | `SYS_set_tid_address`| Set pointer to thread ID for clear_child_tid |
| 227 | `SYS_clock_settime`| Set clock time |
| 228 | `SYS_clock_gettime`| Retrieve clock time (`CLOCK_REALTIME`, `CLOCK_MONOTONIC`) |
| 229 | `SYS_clock_getres` | Retrieve clock resolution |
| 230 | `SYS_clock_nanosleep`| High-precision clock sleep with flags |
| 231 | `SYS_exit_group` | Exit all threads in process |
| 232 | `SYS_epoll_wait` | Wait for I/O events on an epoll file descriptor |
| 233 | `SYS_epoll_ctl` | Control interface for an epoll file descriptor |
| 235 | `SYS_utimes` | Change file timestamps |
| 253 | `SYS_inotify_init` | Initialize inotify instance |
| 254 | `SYS_inotify_add_watch`| Add watch to an initialized inotify instance |
| 255 | `SYS_inotify_rm_watch` | Remove watch from an inotify instance |
| 257 | `SYS_openat` | Open file relative to directory descriptor |
| 258 | `SYS_mkdirat` | Create directory relative to directory descriptor |
| 260 | `SYS_fchownat` | Change ownership relative to directory descriptor |
| 261 | `SYS_futimesat` | Change file timestamps relative to directory descriptor |
| 262 | `SYS_newfstatat` | Retrieve file status relative to directory descriptor |
| 263 | `SYS_unlinkat` | Remove directory entry relative to directory descriptor |
| 265 | `SYS_linkat` | Create hard link relative to directory descriptor |
| 267 | `SYS_readlinkat` | Read symbolic link relative to directory descriptor |
| 268 | `SYS_fchmodat` | Change permissions relative to directory descriptor |
| 269 | `SYS_faccessat` | Check access relative to directory descriptor |
| 280 | `SYS_utimensat` | Update timestamps with nanosecond precision |
| 281 | `SYS_epoll_pwait` | Wait for events on an epoll descriptor with signal mask |
| 282 | `SYS_signalfd` | Create file descriptor for accepting signals |
| 283 | `SYS_timerfd_create`| Create timer notification file descriptor |
| 284 | `SYS_eventfd` | Create file descriptor for event notification |
| 286 | `SYS_timerfd_settime`| Arm or disarm timer referred to by descriptor |
| 287 | `SYS_timerfd_gettime`| Retrieve current timer setting by descriptor |
| 289 | `SYS_signalfd4` | Create signalfd descriptor with specific flags |
| 290 | `SYS_eventfd2` | Create eventfd descriptor with specific flags |
| 291 | `SYS_epoll_create1`| Create an epoll file descriptor with flags |
| 292 | `SYS_dup3` | Duplicate file descriptor with atomic flags (`O_CLOEXEC`) |
| 293 | `SYS_pipe2` | Create unidirectional IPC pipe with atomic flags |
| 294 | `SYS_inotify_init1`| Initialize inotify instance with flags |
| 318 | `SYS_getrandom` | Obtain random bytes from kernel CSPRNG |
| 319 | `SYS_memfd_create` | Create anonymous in-memory file descriptor |
| 362 | `SYS_kqueue` | Allocate kernel event notification queue |
| 363 | `SYS_kevent` | Register events and receive pending notifications |

---

## 5. Build System & Common Commands

All build workflows are managed through the central [Makefile](Makefile) and modular makefiles in `mk/`.

### Architecture of the Build DAG
```
[kernel/include/uapi] ──┐
                        ▼
[libc sources] ────> [libc.a / libc.so] ────> [SYSROOT (/usr/include & /usr/lib)]
                                                     │
               ┌─────────────────────────────────────┼─────────────────────────────┐
               ▼                                     ▼                             ▼
         [libdrm.so]                            [libgbm.so]                [libstdc++.so]
               │                                     │                             │
               └──────────────────┬──────────────────┘                             │
                                  ▼                                                │
                 [Third-Party Ecosystem, X11 & Mesa 3D] <──────────────────────────┘
               (Xorg, Mesa 3D Gallium, libX11, OpenSSH, curl, zlib, ncurses, etc.)
                                  │
                                  ▼
                           [Userland Binaries]
               (init, sh, szpontdesktop, szponterm, startx, coreutils)
                                  │
                                  ▼
                        [USTAR Initramfs Rootfs]
                                  │
      [Kernel ELF] ───────────────┼────────────────> [Bootable ISO Image]
                                                      (via xorriso & Limine)
```

### Toolchain Dependencies & Helper Scripts
- **Compiler:** `x86_64-elf-gcc` (Freestanding cross-compiler)
- **Assembler:** `nasm`
- **Linker:** `x86_64-elf-ld`
- **Archiver:** `x86_64-elf-ar`
- **ISO Generator:** `xorriso`
- **Emulator:** `qemu-system-x86_64`
- **Compilation DB Tool:** `bear`
- **Sysroot Cross-Compilers:** `scripts/szpontos-gcc` and `scripts/szpontos-g++` (target sysroot wrapper scripts for ports and userland)
- **C++ Runtime Builder:** `scripts/build_libstdcxx.py` (automates out-of-tree cross-compilation of GNU libstdc++-v3)

### Standard Build & Run Commands
```bash
# Build complete bootable ISO image
make iso

# Run SzpontOS with Virtio-VGA acceleration (Recommended for desktop / X11)
make run-virtio

# Run SzpontOS in standard VGA graphical window
make run

# Run SzpontOS in headless CLI mode (serial output piped to terminal)
make run-cli

# Run SzpontOS in bare-metal PS/2 keyboard/mouse mode (i8042 enabled)
make run-ps2

# Run SzpontOS in pure modern UEFI USB mode (i8042 disabled, pure xHCI HID)
make run-usb

# Run with realistic hardware timing and virtual instruction counters (-icount)
make run-stress

# Run with GDB debugging stub enabled (listening on localhost:1234)
make debug

# Regenerate compile_commands.json for clangd and IDE IntelliSense
make compile-commands # or make bear

# Clean all build artifacts, objects, and rootfs
make clean
```

### Guest Network & Forwarded Ports
When launched with QEMU, the user-mode SLIRP network forwarders are active:
| Guest Service | Guest Port | Host Forwarded Port | Connection Command |
|---|---|---|---|
| HTTP Web Server | 80 | `8080` (or next free) | `curl http://localhost:8080/` |
| OpenSSH Server | 22 | `2222` (or next free) | `ssh -p 2222 root@localhost` |

---

## 6. Coding & Development Guidelines for AI Agents

1. **Freestanding Environment:**
   - Kernel and Libc code must remain 100% freestanding (`-ffreestanding -fno-builtin -nostdlib`).
   - Do not include host standard library headers. Use `<stdint.h>`, `<stddef.h>`, `<stdbool.h>`, `<stdarg.h>` from `kernel/include/` or `libc/include/`.
   - Types must follow the standard 64-bit **LP64** model (`long` and `unsigned long` are 64 bits; `size_t` and `uintptr_t` are `unsigned long`).

2. **Concurrency & Thread Safety:**
   - Protect global kernel structures (process lists, runqueues, memory maps, VFS tables, DRM buffers) with spinlocks (`spinlock_t`, `spinlock_acquire`, `spinlock_release`).
   - Keep critical sections as short as possible. Never block or sleep while holding a spinlock.

3. **Memory Safety & Higher-Half Access:**
   - Always convert physical frame pointers to higher-half virtual addresses using `PHYS_TO_VIRT(phys)` before dereferencing in kernel code.
   - When modifying page tables for processes, invalidate TLB entries where appropriate (`invlpg`).
   - Always mask page table entries with `PHYS_ADDR_MASK` (`0x000FFFFFFFFFF000ULL`).

4. **Safe Userland Memory Copy (`usercopy`):**
   - **Never direct-dereference user pointers in Ring 0.**
   - Always use `copy_from_user` and `copy_to_user` (`<mm/usercopy.h>`).
   - Validate user-supplied buffers, strings, and sizes before copying. Return `-EFAULT` immediately on invalid addresses.

5. **Assembly Offsets & Hardware ABI Invariants:**
   - Invariants in `cpu_t` ([kernel/include/kernel/smp.h](kernel/include/kernel/smp.h)) are referenced by hard-coded numeric offsets in [kernel/arch/x86_64/syscall_entry.asm](kernel/arch/x86_64/syscall_entry.asm) and [context.asm](kernel/arch/x86_64/context.asm).
   - If any member is added or modified in `cpu_t`, you **must** update the corresponding numeric offsets in the assembly files.

6. **IDE, Clangd & Sysroot Synchronization:**
   - Whenever new C source files are added to `kernel/`, `libc/`, or `userland/`, update the corresponding file list in `Makefile` and run `make compile-commands`.
   - Any new library headers must be installed into `$(SYSROOT_DIR)/usr/include` so downstream userland and third-party packages can compile against them.

7. **Dynamic Linking & Shared Libraries (No Static Linking):**
   - Userland binaries and ported packages must be dynamically linked against shared libraries (`.so`).
   - Avoid static linking for userland programs whenever possible.
   - Core minimal rescue libraries (`libc.so`, `libm.so`, and kernel modules in `/lib/modules/`) reside in `/lib`. Extended and third-party shared libraries reside in `/usr/lib` (e.g. `libz.so`, `libX11.so`, `libpixman-1.so`, `libdrm.so`, `libgbm.so`, `libstdc++.so`, `libgallium-25.0.5.so`). The kernel dynamic ELF loader automatically resolves dependencies across `/lib` and `/usr/lib`.
   - When introducing a new shared library, add it to `ALL_ROOTFS_SOS` in [mk/third_party.mk](mk/third_party.mk).

8. **C++ Runtime & Modern Language Support:**
   - C++ applications and tests must link against `libstdc++.so` or `libstdc++.a`.
   - The runtime supports global constructors/destructors, exceptions, and RTTI. Ensure new C++ binaries call proper runtime entrypoints.

9. **No Stubs / Complete Implementation Rule:**
   - **Never create stubs or mock functions.** Every function must have a complete, robust, and working implementation.
   - Do not leave empty function bodies, placeholder `TODO` comments, dummy return values (e.g. returning dummy success/failure or `-ENOSYS` merely to pass compilation), or partial implementations.
   - All functions, drivers, system calls, and library routines introduced or edited must be fully implemented and functional.

10. **Third-Party Submodules & Patching Policy:**
    - **Never edit code directly in `third_party/` if it is a git submodule.** Submodule working trees must remain clean and track upstream commits without untracked changes, local edits, or dirty state.
    - **Always solve issues at the OS level first:** If a ported third-party package fails to compile, link, or run due to missing POSIX headers, system calls, ioctls, types, constants, socket options, or C library functions, **always implement the missing functionality directly in SzpontOS (`kernel/` or `libc/`)**. The operating system must evolve to adapt to standard software, not the other way around.
    - **Use patches strictly as a last resort:** Only if an issue fundamentally cannot be resolved at the OS level (e.g., hardcoded host tool paths in package build systems, unsupported compiler flags, or non-standard upstream assumptions), create cleanly isolated patch files applied non-destructively during the build process, rather than modifying files directly inside the submodule directory.

11. **No Absolute Host Paths (Portability & Hermetic Builds):**
    - **Never hardcode absolute host filesystem paths** (such as `/Users/...`, `/home/...`, `/opt/...`, or machine-specific developer directories) anywhere in source code, headers, Makefiles, helper scripts, configuration templates, cross-compilation definition files, or test suites.
    - All paths in the codebase and build pipeline must be relative to the repository root or derived dynamically at runtime:
      - In Makefiles: use variables derived from the root, e.g. `$(ROOT_DIR)`, `$(abspath $(ROOT_DIR))`, `$(BUILD_DIR)`, `$(SYSROOT_DIR)`.
      - In shell/python scripts: compute locations dynamically relative to the script file (e.g. `$(cd "$(dirname "$0")/.." && pwd)` or `Path(__file__).resolve().parent`).
      - For host tools and compilers: locate them via dynamic `PATH` lookups (`command -v <tool>`, `which <tool>`, or `pkg-config`) instead of hardcoding absolute binary paths.
    - The repository and build pipeline must remain completely portable, relocatable, and buildable across different developer machines, operating systems, and CI/CD environments.

12. **UAPI Headers Separation & Sysroot Cleanliness:**
    - Kernel-facing and Linux-compatible ioctl definitions, hardware constants, and system types belong strictly in [kernel/include/uapi/](kernel/include/uapi/) (under `linux/` or `asm/`).
    - The C standard library ([libc/include/](libc/include/)) must remain a clean, freestanding POSIX/C17/BSD implementation. Never pollute `libc/include/` with kernel-internal or Linux-specific driver definitions.
    - The root build system exports `kernel/include/uapi/` directly into `$(SYSROOT_DIR)/usr/include/` so that userland binaries and ported packages (such as Mesa 3D, Xorg, libdrm) can include `<linux/...>` or `<asm/...>` headers cleanly.

13. **Process Group Isolation & TTY Signal Routing Invariants:**
    - When modifying process scheduling, session creation (`setsid`), process group manipulation (`setpgid`), or PTY/TTY drivers, always enforce strict process group isolation.
    - Keyboard interrupt signals (`SIGINT`, `SIGQUIT`, `SIGTSTP`) generated by the line discipline must **exclusively** be delivered to the active foreground process group (`tty->pgrp`) via `kill_pgrp()`.
    - Never broadcast terminal signals across the entire session or to background process groups. Background daemons, parent init processes, the X11 server, and window managers must remain immune to terminal Ctrl+C interrupts.

14. **Non-Blocking I/O & Multiplexing Compliance (`poll` / `epoll` / `kqueue`):**
    - Every newly implemented character device, pseudo-filesystem node, IPC primitive (pipes, eventfd, timerfd, signalfd), or network socket that can block on read/write **must** implement a corresponding `poll` callback in its `vfs_node_ops_t`.
    - Drivers must correctly indicate readability (`POLLIN | POLLRDNORM`) and writability (`POLLOUT | POLLWRNORM`) based on immediate buffer state without sleeping.
    - State transitions (e.g., incoming network frame, pipe buffer write, timer expiration) must reliably notify all registered waitqueues so that `epoll_wait()`, `kevent()`, and `select()` wake up immediately.

15. **Virtual Memory Safety, Page Alignment & COW Integrity:**
    - All virtual memory mappings (`mmap`), protection modifications (`mprotect`), and page unmappings (`munmap`) must strictly enforce page-alignment boundaries (`PAGE_SIZE = 4096`).
    - Anonymous memory must be zero-filled on demand to prevent kernel or previous-process data leaks.
    - Copy-On-Write (COW) page forks must mark page table entries in both parent and child as read-only. The page fault handler must allocate a fresh physical frame, copy the 4096 bytes via HHDM, and update the faulting process's page table before resuming execution.
    - Never permit userland address mappings to extend beyond canonical user limits (`USER_ADDR_MAX = 0x00007FFFFFFFFFFFULL`) or overlap with higher-half kernel space.

16. **Device Driver Hardware Sequencing & Idle Thread Non-Starvation:**
    - In interrupt-driven device drivers (NICs, USB host controllers, disk controllers, timers), interrupt handlers must acknowledge hardware status registers **before** sending End-Of-Interrupt (EOI) to the local APIC or PIC, preventing interrupt storms and dropped events on real hardware.
    - Kernel syscall routines and driver wait-loops must never spin in tight busy-waiting loops. Always use waitqueues or `thread_sleep()` so the scheduler can yield CPU cores to `g_idle_thread` to halt (`hlt`) and allow hardware timer ticks to advance.

17. **GUI & Window Manager Protocol Hygiene (Szpont Experience):**
    - When developing or modifying graphical applications, desktop components, or window manager code (`szpontdesktop`, `szponterm`, `szpontlogin`), strictly adhere to X11 client-server protocol specifications:
      - Window managers must properly intercept and manage client windows using `SubstructureRedirectMask` and `SubstructureNotifyMask`.
      - Applications must support the `WM_DELETE_WINDOW` protocol message for clean exit handling instead of abrupt disconnects.
      - Free all allocated X server resources (GContexts, Pixmaps, Colormaps, XShape masks) upon window destruction to prevent memory and handle exhaustion across long-running sessions.
