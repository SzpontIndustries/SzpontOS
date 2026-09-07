# SzpontOS — Developer & Agent Reference Manual

> **Branding & Copyright:**  
> `(C) Copyright by Szpont Industries. All rights reserved.`  
> **Target Architecture:** `x86_64` (Higher-Half Bare Metal & Ring 3 Userland)  
> **Boot Protocol:** Limine Boot Protocol v8.x (BIOS & UEFI support)

---

## 1. Project Overview

**SzpontOS** is an independent, 64-bit Unix-like operating system written from scratch in C17 and x86_64 Assembly. It features a higher-half monolithic kernel, preemptive multitasking, hardware-assisted fast system calls (`syscall`/`sysret`), virtual memory management with userland isolation (Ring 3), a modular Virtual File System (VFS/DevFS/ProcFS/Ext2/Initramfs), a freestanding C Standard Library (`libc.a`, `libm.so`, `libdl.a`), a complete BSD sockets TCP/IP network stack (with Intel E1000 NIC driver), hardware CMOS RTC & TSC precision timing, dynamically loaded kernel modules (`.sko`), and native userland ELF binaries (including `/bin/init`, `/bin/sh`, `/bin/nano`, `/bin/zsh`, `/bin/fastfetch`).

---

## 2. Directory Structure

```
SzpontOS/
├── kernel/                      # Higher-Half Operating System Kernel
│   ├── arch/x86_64/             # Architecture-specific CPU initialization
│   │   ├── gdt.c / idt.c        # GDT, TSS, IDT, 256 interrupt gates
│   │   ├── pic.c / pit.c        # 8259 PIC remapping, PIT 100 Hz timer
│   │   ├── isr.asm              # Interrupt Service Routines & CPU exception handlers
│   │   ├── context.asm          # arch_switch_context & arch_enter_user_mode (iretq)
│   │   ├── syscall_arch.c       # Fast syscall MSR initialization (EFER.SCE, STAR, LSTAR)
│   │   └── syscall_entry.asm    # Syscall hardware entry, kernel stack switch & sysret
│   ├── include/                 # Kernel internal headers
│   │   ├── arch/x86_64/         # GDT, IDT, PIC, PIT, IO port primitives, CPU registers
│   │   ├── drivers/             # FB console, UART COM1, PS/2 keyboard, CMOS RTC, ATA, E1000, PCI
│   │   ├── fs/                  # VFS, DevFS, ProcFS, Ext2, Buffer Cache, Initramfs, ELF-64 loader
│   │   ├── mm/                  # PMM (bitmap), VMM (4-level paging), Heap (slab/buddy)
│   │   ├── mod/                 # Loadable Kernel Modules (.sko) subsystem
│   │   ├── net/                 # Ethernet, ARP, IPv4, ICMP, UDP, TCP, Netdev, Sockets
│   │   ├── sched/               # process_t, thread_t, futex, preemptive Round-Robin scheduler
│   │   ├── syscall/             # POSIX syscall dispatcher and prototypes
│   │   ├── kernel/              # Types, kprint, panic, spinlocks, string utilities
│   │   └── limine.h             # Limine bootloader protocol specification
│   ├── src/                     # Kernel core implementation
│   │   ├── main.c               # Kernel entry point (_start) and subsystem initialization
│   │   ├── string.c / kprint.c  # Kernel string library, kprintf, ksnprintf, klog
│   │   ├── panic.c              # Kernel panic handler and register dump
│   │   ├── drivers/             # serial.c, framebuffer.c, font8x16.c, keyboard.c, rtc.c, ata.c, pci.c, e1000.c
│   │   ├── fs/                  # vfs.c, devfs.c, procfs.c, ext2.c, bcache.c, initramfs.c, elf.c
│   │   ├── mm/                  # pmm.c, vmm.c, heap.c
│   │   ├── mod/                 # module.c (ELF module loader, symbol resolution, relocations)
│   │   ├── net/                 # netif.c, ethernet.c, arp.c, ipv4.c, icmp.c, udp.c, tcp.c, socket.c
│   │   ├── sched/               # process.c, sched.c, futex.c
│   │   └── syscall/             # syscall.c
│   └── linker.ld                # Higher-half linker script (0xFFFFFFFF80000000)
│
├── libc/                        # Freestanding C Standard Library (builds libc.a, libm.so, libdl.a)
│   ├── include/                 # Standard POSIX headers (100% clean POSIX/C17/BSD)
│   │   ├── stdio.h, stdlib.h, string.h, unistd.h, fcntl.h, dirent.h, errno.h, time.h
│   │   ├── pthread.h, semaphore.h, dlfcn.h, math.h, termios.h, poll.h, signal.h
│   │   ├── stdint.h, stddef.h, stdbool.h, stdarg.h (LP64 self-contained headers)
│   │   └── sys/ (stat.h, types.h, socket.h, mman.h, poll.h, utsname.h, wait.h, time.h)
│   └── src/
│       ├── arch/x86_64/         # crt0.asm, syscall.asm, setjmp.asm
│       ├── stdio/               # printf.c, snprintf.c, puts, putchar, getchar, file ops
│       ├── stdlib/              # malloc.c (sbrk heap allocator), strtol, atoi, env
│       ├── string/              # Standard string and memory manipulation routines
│       ├── time/                # time.c (clock_gettime, gettimeofday, time, nanosleep)
│       ├── pthread/             # POSIX threads, mutexes, condvars, barriers, semaphores, TLS
│       ├── socket/              # Berkeley Sockets API (socket, connect, bind, listen, recv, send)
│       ├── netdb/               # getaddrinfo, gethostbyname, DNS resolver
│       └── unistd/              # POSIX syscall wrappers (fork, execve, read, write, sleep, etc.)
│
├── third_party/                 # Ported open-source packages cross-compiled against libc sysroot
│   ├── ncurses/                 # GNU Ncurses (libncurses.a)
│   ├── nano/                    # GNU nano editor (/bin/nano)
│   ├── file/                    # Libmagic & GNU file (/bin/file)
│   ├── zsh/                     # Z shell (/bin/zsh)
│   └── fastfetch/               # System information tool (/bin/fastfetch)
│
├── libdrm/                      # Native Direct Rendering Manager Library (builds libdrm.so.2)
│   ├── include/                 # xf86drm.h, xf86drmMode.h, drm/*
│   └── src/                     # xf86drm.c, xf86drmMode.c, syncobj.c
│
├── userland/                    # User space programs and root filesystem
│   ├── init/main.c              # PID 1 init process (spawns /bin/sh)
│   ├── sh/main.c                # Interactive Unix shell with built-ins & history
│   ├── bin/                     # Core utilities: cat, clear, df, free, hello, hostname, httpd,
│   │                            # id, ifconfig, insmod, lsmod, mathtest, mkdir, modinfo,
│   │                            # mount, nc, ping, ps, rm, rmmod, sleep, su, tail,
│   │                            # threadtest, touch, tuitest, uname, uptime, wc, whoami
│   └── skeleton/                # Static rootfs skeleton templates (/etc/passwd, /etc/magic, etc.)
│
├── scripts/                     # Toolchain & execution helper scripts
│   ├── run_qemu.sh              # QEMU launcher (supports --virtio, --headless, --debug)
│   ├── make_initramfs.py        # Python script packaging rootfs into USTAR initramfs.tar
│   └── generate_ncurses_fallbacks.py
│
├── limine.conf                  # Limine bootloader boot configuration
├── Makefile                     # Unified build system (kernel, libc, third_party, userland, ISO)
├── .clangd                      # Clangd language server bare-metal configuration
└── .vscode/c_cpp_properties.json# IDE include paths and IntelliSense configuration
```

---

## 3. Core Technical Principles & Architecture

### 3.1 Memory Layout (x86_64 4-Level Paging)
- **Higher-Half Kernel Base:** `0xFFFFFFFF80000000` (mapped via linker script and Limine).
- **HHDM (Higher-Half Direct Map Base):** `0xFFFF800000000000` (physical memory offset `g_hhdm_base`).
- **Userland Virtual Address Space:** `0x0000000000400000` – `0x00007FFFFFFFFFFF` (Ring 3, DPL=3).
- **User Stack Base:** `0x00007FFFF0000000` (grows down, default size 64 KiB).
- **User Heap (`brk`):** Starts at `0x0000000000800000` and dynamically expands via `sys_brk`.

### 3.2 Physical Address Masking Rule
> [!IMPORTANT]
> When traversing or modifying 64-bit page table entries (PML4, PDPT, PD, PT), **always** mask using `PHYS_ADDR_MASK` (`0x000FFFFFFFFFF000ULL`).
> Never use `~0xFFFULL`, as bit 63 (`NX` / No Execute) or other high architectural bits will corrupt the physical pointer converted via `PHYS_TO_VIRT()`.

### 3.3 Hardware Interrupts, PIC EOI & Idle Loop
1. **Interrupt Handler then EOI Ordering:** In `kernel/arch/x86_64/idt.c`, registered interrupt handlers are executed **before** sending EOI (PIC `outb(0x20, 0x20)` or LAPIC EOI). This ensures hardware device handlers (e.g. keyboard port `0x60`) can read data before the next interrupt is allowed. On bare metal hardware, sending EOI before the handler causes scancode loss due to real IO-APIC/LAPIC timing.
2. **Idle Thread CPU Halt:** In `kernel/src/sched/sched.c`, `g_idle_thread` executes `__asm__ volatile ("sti; hlt; cli");`. This ensures the CPU halts in low-power state with interrupts enabled so the PIT timer IRQ0 can wake it.
3. **Blocking vs Spinning:** Never spin in a tight `sched_yield()` loop in syscalls. For blocking operations (e.g. `waitpid`), use `thread_sleep(10)` so the scheduler can yield to `idle_thread` and allow timer ticks (`g_pit_ticks`) to advance.

### 3.4 Hardware RTC & CPU TSC Precision Timing
- **CMOS RTC:** [kernel/src/drivers/rtc.c](kernel/src/drivers/rtc.c) reads ports `0x70`/`0x71` with Update-In-Progress (`UIP`) polling and BCD conversion to Unix epoch.
- **TSC Calibration:** At boot, TSC frequency is calibrated against PIT hardware counter latch (port `0x40`) without requiring interrupts (`g_tsc_freq_hz`).
- **Monotonic High-Precision Time:** `rtc_get_monotonic()` / `sys_clock_gettime(CLOCK_MONOTONIC)` computes time directly from `rdtsc()`, providing sub-microsecond precision (used by `ping`, `sleep`, profiling).

### 3.5 Networking Stack & NIC Driver (Intel 8254x / E1000)
- The network stack supports Ethernet, ARP, IPv4, ICMP, UDP, and TCP (state machine with 3-way handshake).
- **Packet Polling Rule:** During socket reads (`recvfrom`, `read`) and `poll`, `e1000_poll()` is called immediately to process incoming frames from SLIRP without waiting for the 100 Hz PIT timer tick.

### 3.6 Ring 3 Transition & Fast Syscalls
- **Switching to User Mode:** Handled in [kernel/arch/x86_64/context.asm](kernel/arch/x86_64/context.asm) (`arch_enter_user_mode`) by setting selectors `CS=0x23`, `DS/ES/FS/GS=0x1B`, `SS=0x1B`, `RFLAGS=0x202`, and executing `iretq`.
- **Fast Syscalls:** Handled via `syscall` / `sysretq`. Hardware jumps to `syscall_entry` in [kernel/arch/x86_64/syscall_entry.asm](kernel/arch/x86_64/syscall_entry.asm), saves user RSP, switches to `g_current_kernel_stack`, and maps arguments according to System V AMD64 ABI:
  - `RAX` (sys_no) $\rightarrow$ `RDI`
  - `RDI` (arg1) $\rightarrow$ `RSI`
  - `RSI` (arg2) $\rightarrow$ `RDX`
  - `RDX` (arg3) $\rightarrow$ `RCX`
  - `R10` (arg4) $\rightarrow$ `R8`
  - `R8`  (arg5) $\rightarrow$ `R9`

---

## 4. Syscall Reference Table

| Syscall Number | Name | Description |
|---|---|---|
| 0 | `SYS_read` | Read from file descriptor |
| 1 | `SYS_write` | Write to file descriptor |
| 2 | `SYS_open` | Open file or device |
| 3 | `SYS_close` | Close file descriptor |
| 4 | `SYS_stat` | Get file status by pathname |
| 5 | `SYS_fstat` | Get file status by descriptor |
| 7 | `SYS_poll` | Poll file descriptors for I/O events |
| 8 | `SYS_lseek` | Reposition read/write file offset |
| 9 | `SYS_mmap` | Map pages into process address space |
| 11 | `SYS_munmap` | Unmap pages from process address space |
| 12 | `SYS_brk` | Expand or contract heap breakpoint |
| 16 | `SYS_ioctl` | Device control operations (TTY/FB/Window size) |
| 22 | `SYS_pipe` | Create unidirectional data channel pipe |
| 23 | `SYS_select` | Synchronous I/O multiplexing |
| 32 | `SYS_dup` / `SYS_dup2` | Duplicate open file descriptor |
| 35 | `SYS_nanosleep` | High-precision thread sleep |
| 39 | `SYS_getpid` / `SYS_getppid` | Process identification |
| 41 | `SYS_socket` | Create communication endpoint |
| 42 | `SYS_connect` | Initiate connection on socket |
| 43 | `SYS_accept` | Accept connection on socket |
| 44 | `SYS_sendto` | Send message on socket |
| 45 | `SYS_recvfrom` | Receive message from socket |
| 49 | `SYS_bind` / `SYS_listen` | Bind socket and listen for connections |
| 56 | `SYS_clone` | Create thread/process (supports `CLONE_VM`, `CLONE_THREAD`, `CLONE_FS`) |
| 57 | `SYS_fork` | Clone calling process with COW/full address space copy |
| 59 | `SYS_execve` | Execute ELF binary with arguments and environment |
| 60 | `SYS_exit` / `SYS_exit_group` | Terminate thread / process |
| 61 | `SYS_wait4` | Wait for process state changes |
| 63 | `SYS_uname` | Get system name and OS version |
| 78 | `SYS_getdents` | Read directory entries into buffer |
| 79 | `SYS_getcwd` / `SYS_chdir` | Get and set current working directory |
| 96 | `SYS_gettimeofday` | Get time with microsecond resolution |
| 101 | `SYS_sleep` | Sleep for specified seconds |
| 158 | `SYS_arch_prctl` | Set thread architecture state (FS_BASE / GS_BASE) |
| 201 | `SYS_time` | Get current Unix timestamp (seconds) |
| 202 | `SYS_futex` | Fast user-space locking (`FUTEX_WAIT`, `FUTEX_WAKE`) |
| 228 | `SYS_clock_gettime` | Retrieve clock time (`CLOCK_REALTIME`, `CLOCK_MONOTONIC`) |

---

## 5. Build System & Common Commands

All build workflows are managed through the central [Makefile](Makefile).

### Toolchain Dependencies
- **Compiler:** `x86_64-elf-gcc` (Freestanding cross-compiler)
- **Assembler:** `nasm`
- **Linker:** `x86_64-elf-ld`
- **Archiver:** `x86_64-elf-ar`
- **ISO Generator:** `xorriso`
- **Emulator:** `qemu-system-x86_64`
- **Compilation DB Tool:** `bear`

### Standard Commands
```bash
# Build complete bootable ISO (builds libc, third_party, userland, kernel, initramfs, and ISO)
make iso

# Run SzpontOS in graphical QEMU window (1920x1080)
make run

# Run SzpontOS in graphical QEMU with Virtio-VGA acceleration (Recommended)
make run-virtio

# Run SzpontOS in headless mode (serial output only in terminal)
make run-cli

# Run SzpontOS with GDB debugging stub enabled (target remote :1234)
make debug

# Regenerate compile_commands.json for clangd and IDE IntelliSense
make compile-commands

# Clean all build artifacts and object files
make clean
```

---

## 6. Coding & Development Guidelines for AI Agents

1. **Freestanding Environment:**
   - Kernel and Libc code must remain 100% freestanding (`-ffreestanding -fno-builtin -nostdlib`).
   - Do not include host standard library headers. Use `<stdint.h>`, `<stddef.h>`, `<stdbool.h>`, `<stdarg.h>` from `kernel/include/` or `libc/include/`.
   - Types must follow the standard 64-bit **LP64** model (`long` and `unsigned long` are 64 bits; `size_t` and `uintptr_t` are `unsigned long`).

2. **Concurrency & Thread Safety:**
   - Protect global kernel structures (process lists, runqueues, memory maps, VFS tables) with spinlocks (`spinlock_t`, `spinlock_acquire`, `spinlock_release`).
   - Keep critical sections as short as possible.

3. **Memory Safety & Higher-Half Access:**
   - Always convert physical frame pointers to higher-half virtual addresses using `PHYS_TO_VIRT(phys)` before dereferencing in kernel code.
   - When modifying page tables for processes, invalidate TLB entries where appropriate (`invlpg`).

4. **IDE & Clangd Compatibility:**
   - Whenever new C source files are added to `kernel/`, `libc/`, or `userland/`, update the corresponding file list in `Makefile` and run `make compile-commands`.
   - Verify syntax with `clang --target=x86_64-unknown-none-elf -fsyntax-only`.

5. **Dynamic Linking & Shared Libraries (No Static Linking):**
   - Userland binaries and ported packages must be dynamically linked against shared libraries (`.so`).
   - Avoid static linking for userland programs whenever possible.
   - All shared libraries must reside in `/lib` (in `build/rootfs/lib/`) with valid ELF `DT_SONAME` tags (e.g. `libc.so`, `libm.so`, `libz.so`, `libX11.so`, `libpixman-1.so`).
   - Third-party packages and libraries must use standard `make install DESTDIR="$(ROOTFS_DIR)"` fakeroot-style installation so binaries, shared objects, headers, and data files are cleanly integrated into the root filesystem.

