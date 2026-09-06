/*
 * SzpontOS - ACPI Power Management & Hardware Reset Implementation
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <drivers/power.h>
#include <drivers/acpi.h>
#include <arch/x86_64/io.h>
#include <fs/bcache.h>
#include <sched/process.h>
#include <sched/sched.h>
#include <kernel/kprint.h>

void power_shutdown(void) {
    klog_info("System is shutting down...");
    bflush(NULL);
    cli();

    /* 1. ACPI S5 native poweroff (Standard for Bare Metal and modern hypervisors) */
    acpi_poweroff();

    /* 2. QEMU ACPI poweroff port */
    outw(0x604, 0x2000);
    io_wait();

    /* 3. QEMU isa-debug-exit port */
    outb(0x501, 0x31);
    io_wait();

    /* 4. VirtualBox ACPI poweroff */
    outw(0x4004, 0x3400);
    io_wait();

    /* 5. Bochs / QEMU older ACPI */
    outw(0xB004, 0x2000);
    io_wait();

    /* 6. Fallback CPU halt */
    while (1) {
        hlt();
    }
}

void power_reboot(void) {
    klog_info("System is rebooting...");
    bflush(NULL);
    cli();

    /*
     * Hardware reset sequence:
     * 1. ACPI 2.0+ Reset Register (Primary method on modern Bare Metal UEFI/BIOS)
     * 2. PCI Reset Control Register (Port 0xCF9) — System Reset, Hard Reset, and PCIe Cold Reset
     * 3. 8042 Keyboard Controller reset pulse (outb 0x64, 0xFE)
     * 4. Fast A20 / Init register (Port 0x92) — assert INIT#
     * 5. Triple Fault (Fail-safe CPU reset with NULL IDT and interrupt)
     */

    /* 1. ACPI Reset Register */
    if (acpi_reboot()) {
        for (volatile int d = 0; d < 5000000; d++) {
        }
    }

    /* 2. PCI Reset Control Register (Port 0xCF9) */
    outb(0xCF9, 0x02);
    io_wait();
    outb(0xCF9, 0x06);
    for (volatile int d = 0; d < 5000000; d++) {
    }
    outb(0xCF9, 0x0E);
    for (volatile int d = 0; d < 5000000; d++) {
    }

    /* 3. 8042 Keyboard Controller reset pulse */
    if (acpi_has_8042_controller()) {
        for (int i = 0; i < 10000; i++) {
            if ((inb(0x64) & 0x02) == 0)
                break;
            io_wait();
        }
        outb(0x64, 0xFE);
        for (volatile int d = 0; d < 5000000; d++) {
        }
    }

    /* 4. Fast A20 / Init register (Port 0x92) — assert INIT# */
    {
        uint8_t b = inb(0x92);
        if (b != 0xFF) {
            if ((b & 0x01) != 0)
                outb(0x92, b & 0xFE);
            outb(0x92, b | 0x01);
        }
    }
    for (volatile int d = 0; d < 5000000; d++) {
    }

    /* 5. Triple Fault — load NULL IDT and trigger interrupt (FreeBSD/Linux standard) */
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) null_idt = {0, 0};

    __asm__ volatile("lidt %0; int $3" ::"m"(null_idt));

    /* NOTREACHED */
    while (1) {
        hlt();
    }
}

int sys_reboot(int magic1, int magic2, int cmd, void *arg) {
    (void)magic2;
    (void)arg;
    process_t *proc = sched_get_current_process();
    if (!proc || proc->euid != 0) {
        return -1; /* EPERM */
    }

    /* Verify reboot magic or permissive execution */
    if (magic1 != (int)REBOOT_MAGIC1 && magic1 != 0) {
        return -1; /* EINVAL */
    }

    switch ((uint32_t)cmd) {
    case REBOOT_CMD_POWER_OFF:
    case REBOOT_CMD_HALT:
        power_shutdown();
        return 0;

    case REBOOT_CMD_RESTART:
    default:
        power_reboot();
        return 0;
    }
}
