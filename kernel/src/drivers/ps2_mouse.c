/*
 * SzpontOS - PS/2 Mouse Driver (i8042 AUX Port / IRQ 12)
 * Inspired by FreeBSD sys/dev/atkbdc/psm.c
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <drivers/ps2_mouse.h>
#include <drivers/mouse.h>
#include <drivers/evdev.h>
#include <drivers/keyboard.h>
#include <drivers/ioapic.h>
#include <drivers/acpi.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/pic.h>
#include <kernel/kprint.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <kernel/sysctl.h>

#define I8042_DATA_PORT 0x60
#define I8042_STATUS_PORT 0x64
#define I8042_COMMAND_PORT 0x64

#define I8042_STATUS_OBF (1 << 0)
#define I8042_STATUS_IBF (1 << 1)
#define I8042_STATUS_AUX_OBF (1 << 5)

#define I8042_CMD_READ_CONFIG 0x20
#define I8042_CMD_WRITE_CONFIG 0x60
#define I8042_CMD_DISABLE_AUX 0xA7
#define I8042_CMD_ENABLE_AUX 0xA8
#define I8042_CMD_TEST_AUX 0xA9
#define I8042_CMD_WRITE_AUX 0xD4

#define MOUSE_CMD_SET_SCALE11 0xE6
#define MOUSE_CMD_SET_SCALE21 0xE7
#define MOUSE_CMD_SET_RES 0xE8
#define MOUSE_CMD_STATUS_REQ 0xE9
#define MOUSE_CMD_SET_STREAM 0xEA
#define MOUSE_CMD_GET_ID 0xF2
#define MOUSE_CMD_SET_SAMPLE 0xF3
#define MOUSE_CMD_ENABLE_DATA 0xF4
#define MOUSE_CMD_DISABLE_DATA 0xF5
#define MOUSE_CMD_SET_DEFAULT 0xF6
#define MOUSE_CMD_RESET 0xFF

#define MOUSE_RESP_ACK 0xFA
#define MOUSE_RESP_BAT_OK 0xAA

#define MOUSE_QUEUE_SIZE 256

static mouse_packet_t g_mouse_queue[MOUSE_QUEUE_SIZE];
static size_t g_mouse_read_ptr = 0;
static size_t g_mouse_write_ptr = 0;
static spinlock_t g_mouse_lock = SPINLOCK_INIT;

static uint8_t g_packet_bytes[4];
static uint8_t g_packet_idx = 0;
static bool g_has_wheel = false;
static bool g_mouse_initialized = false;

/*
 * PS/2 Mouse Y Inversion Setting:
 * -1 = Auto-detect (invert on bare-metal hardware, preserve on QEMU / hypervisors)
 *  0 = Off (Standard QEMU / hypervisor virtual pointer orientation)
 *  1 = On  (Invert Cartesian Y displacement for legacy bare-metal hardware)
 */
static int g_ps2_invert_y = -1;

static bool is_hypervisor(void) {
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));
    return (ecx & (1U << 31)) != 0;
}

static bool should_invert_y(void) {
    if (g_ps2_invert_y == 1)
        return true;
    if (g_ps2_invert_y == 0)
        return false;
    /* Auto-detect: invert only on real bare metal */
    return !is_hypervisor();
}

static bool ps2_mouse_send_cmd(uint8_t cmd) {
    return keyboard_aux_command(cmd);
}

static bool ps2_mouse_set_sample_rate(uint8_t rate) {
    return ps2_mouse_send_cmd(MOUSE_CMD_SET_SAMPLE) && ps2_mouse_send_cmd(rate);
}

static void mouse_enqueue_packet(const mouse_packet_t *pkt) {
    spinlock_acquire(&g_mouse_lock);
    size_t next = (g_mouse_write_ptr + 1) % MOUSE_QUEUE_SIZE;
    if (next != g_mouse_read_ptr) {
        g_mouse_queue[g_mouse_write_ptr] = *pkt;
        g_mouse_write_ptr = next;
    }
    spinlock_release(&g_mouse_lock);
}

#include <drivers/mouse.h>

void ps2_mouse_handle_byte(uint8_t byte) {
    /* Byte 0 must have bit 3 always set (Sync bit) */
    if (g_packet_idx == 0 && !(byte & 0x08)) {
        /* Discard out-of-sync byte — NEVER forward to keyboard handler */
        return;
    }

    g_packet_bytes[g_packet_idx++] = byte;
    uint8_t max_bytes = g_has_wheel ? 4 : 3;

    if (g_packet_idx >= max_bytes) {
        g_packet_idx = 0;

        uint8_t flags = g_packet_bytes[0];
        int32_t dx = g_packet_bytes[1];
        int32_t dy = g_packet_bytes[2];
        int8_t dz = 0;

        /* Sign extension */
        if (flags & 0x10)
            dx |= 0xFFFFFF00; /* X sign bit */
        if (flags & 0x20)
            dy |= 0xFFFFFF00; /* Y sign bit */

        /*
         * PS/2 hardware reports Y displacement in Cartesian coordinates (positive = UP, negative = DOWN).
         * Standard OS / evdev / screen coordinates use positive = DOWN, negative = UP.
         * QEMU/KVM virtual pointers already emit screen coordinates, whereas real bare-metal hardware
         * emits Cartesian Y deltas. should_invert_y() dynamically reconciles this difference.
         */
        if (should_invert_y()) {
            dy = -dy;
        }

        if (g_has_wheel) {
            dz = (int8_t)g_packet_bytes[3];
            if (dz & 0x80)
                dz |= (int8_t)0xF0;
            dz &= 0x0F;
            if (dz & 0x08)
                dz -= 16;
        }

        mouse_packet_t pkt;
        pkt.buttons = flags & 0x07;
        pkt.dx = dx;
        pkt.dy = dy;
        pkt.dz = dz;

        mouse_enqueue_packet(&pkt);

        /* Push to generic mouse subsystem (which bridges to evdev and /dev/input/mice) */
        mouse_event_t ev;
        ev.buttons = flags & 0x07;
        ev.dx = dx;
        ev.dy = dy;
        ev.dz = dz;
        ev.abs_x = 0;
        ev.abs_y = 0;
        ev.is_absolute = false;
        mouse_push_event(&ev);
    }
}

static void mouse_irq_handler(interrupt_frame_t *frame) {
    UNUSED(frame);
    keyboard_poll_hardware();
}

void ps2_mouse_init(void) {
    spinlock_init(&g_mouse_lock);
    uint64_t flags = keyboard_controller_acquire();
    g_mouse_initialized = false;
    g_packet_idx = 0;
    if (!keyboard_configure_aux(false))
        goto no_mouse;
    /* bool must be tested for false; the previous '< 0' never detected
     * failed reset and then consumed keyboard input as mouse replies. */
    if (!ps2_mouse_send_cmd(MOUSE_CMD_RESET))
        goto no_mouse;
    if (keyboard_aux_read(500000) != MOUSE_RESP_BAT_OK)
        goto no_mouse;
    int dev_id = keyboard_aux_read(100000);
    if (dev_id < 0)
        goto no_mouse;
    g_has_wheel = false;
    if (ps2_mouse_set_sample_rate(200) && ps2_mouse_set_sample_rate(100) &&
        ps2_mouse_set_sample_rate(80) && ps2_mouse_send_cmd(MOUSE_CMD_GET_ID)) {
        int id = keyboard_aux_read(100000);
        g_has_wheel = id == 3 || id == 4;
    }
    if (!ps2_mouse_set_sample_rate(100) || !ps2_mouse_send_cmd(MOUSE_CMD_SET_RES) ||
        !ps2_mouse_send_cmd(3))
        goto no_mouse;
    isr_register_handler(IRQ12, mouse_irq_handler);
    if (!ps2_mouse_send_cmd(MOUSE_CMD_ENABLE_DATA))
        goto no_mouse;
    g_mouse_initialized = true;
    if (!keyboard_configure_aux(true)) {
        g_mouse_initialized = false;
        goto no_mouse;
    }
    if (!ioapic_is_active()) {
        pic_clear_mask(2);
        pic_clear_mask(12);
    }
    ioapic_map_irq(12, IRQ12, 0, false, false);
    keyboard_controller_release(flags);
    sysctl_register("hw.ps2_mouse.invert_y", CTLTYPE_INT, CTLFLAG_RW, &g_ps2_invert_y, sizeof(int), NULL,
                    "Invert PS/2 Mouse Y Axis (-1=Auto, 0=Off, 1=On)");
    klog_info("PS/2 Mouse: initialized (wheel=%d); keyboard translation preserved", g_has_wheel);
    return;

no_mouse:
    /* Keep both clocks running, mask only AUX IRQ, preserve negotiated XLATE. */
    keyboard_configure_aux(false);
    keyboard_controller_release(flags);
    klog_info("PS/2 Mouse: absent or unresponsive; keyboard configuration preserved");
}

bool ps2_mouse_is_enabled(void) {
    return g_mouse_initialized;
}

bool ps2_mouse_has_packet(void) {
    spinlock_acquire(&g_mouse_lock);
    bool has_pkt = (g_mouse_read_ptr != g_mouse_write_ptr);
    spinlock_release(&g_mouse_lock);
    return has_pkt;
}

bool ps2_mouse_get_packet(mouse_packet_t *pkt) {
    if (!pkt)
        return false;
    spinlock_acquire(&g_mouse_lock);
    if (g_mouse_read_ptr == g_mouse_write_ptr) {
        spinlock_release(&g_mouse_lock);
        return false;
    }
    *pkt = g_mouse_queue[g_mouse_read_ptr];
    g_mouse_read_ptr = (g_mouse_read_ptr + 1) % MOUSE_QUEUE_SIZE;
    spinlock_release(&g_mouse_lock);
    return true;
}

ssize_t ps2_mouse_devfs_read(void *buf, size_t count) {
    if (!buf || count < 3)
        return 0;

    mouse_packet_t pkt;
    if (ps2_mouse_get_packet(&pkt)) {
        if (count >= sizeof(mouse_packet_t)) {
            memcpy(buf, &pkt, sizeof(mouse_packet_t));
            return sizeof(mouse_packet_t);
        }

        uint8_t *dst = (uint8_t *)buf;
        uint8_t flags = 0x08 | (pkt.buttons & 0x07);
        if (pkt.dx < 0) flags |= 0x10;
        if (pkt.dy < 0) flags |= 0x20;
        dst[0] = flags;
        dst[1] = (uint8_t)(pkt.dx & 0xFF);
        dst[2] = (uint8_t)(pkt.dy & 0xFF);
        if (count >= 4 && g_has_wheel) {
            dst[3] = (uint8_t)(pkt.dz & 0x0F);
            return 4;
        }
        return 3;
    }
    return 0;
}

