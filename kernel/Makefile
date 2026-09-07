# SzpontOS Kernel Makefile
ROOT_DIR ?= $(abspath ..)
include $(ROOT_DIR)/config.mk

KERNEL_C_SRCS := \
    $(ROOT_DIR)/kernel/src/main.c \
    $(ROOT_DIR)/kernel/src/string.c \
    $(ROOT_DIR)/kernel/src/kprint.c \
    $(ROOT_DIR)/kernel/src/panic.c \
    $(ROOT_DIR)/kernel/src/mm/pmm.c \
    $(ROOT_DIR)/kernel/src/mm/vmm.c \
    $(ROOT_DIR)/kernel/src/mm/heap.c \
    $(ROOT_DIR)/kernel/src/mm/shm.c \
    $(ROOT_DIR)/kernel/src/mm/usercopy.c \
    $(ROOT_DIR)/kernel/src/fs/vfs.c \
    $(ROOT_DIR)/kernel/src/fs/devfs.c \
    $(ROOT_DIR)/kernel/src/fs/bcache.c \
    $(ROOT_DIR)/kernel/src/fs/ext2.c \
    $(ROOT_DIR)/kernel/src/fs/initramfs.c \
    $(ROOT_DIR)/kernel/src/fs/procfs.c \
    $(ROOT_DIR)/kernel/src/fs/tmpfs.c \
    $(ROOT_DIR)/kernel/src/fs/elf.c \
    $(ROOT_DIR)/kernel/src/sched/process.c \
    $(ROOT_DIR)/kernel/src/sched/futex.c \
    $(ROOT_DIR)/kernel/src/sched/sched.c \
    $(ROOT_DIR)/kernel/src/sched/waitqueue.c \
    $(ROOT_DIR)/kernel/src/kernel/module.c \
    $(ROOT_DIR)/kernel/src/kernel/ksymtab.c \
    $(ROOT_DIR)/kernel/src/kernel/sysctl.c \
    $(ROOT_DIR)/kernel/src/kernel/kqueue.c \
    $(ROOT_DIR)/kernel/src/syscall/syscall.c \
    $(ROOT_DIR)/kernel/src/drivers/serial.c \
    $(ROOT_DIR)/kernel/src/drivers/power.c \
    $(ROOT_DIR)/kernel/src/drivers/random.c \
    $(ROOT_DIR)/kernel/src/drivers/pty.c \
    $(ROOT_DIR)/kernel/src/drivers/font8x16.c \
    $(ROOT_DIR)/kernel/src/drivers/framebuffer.c \
    $(ROOT_DIR)/kernel/src/drivers/keyboard.c \
    $(ROOT_DIR)/kernel/src/drivers/mouse.c \
    $(ROOT_DIR)/kernel/src/drivers/ps2_mouse.c \
    $(ROOT_DIR)/kernel/src/drivers/ps2_test.c \
    $(ROOT_DIR)/kernel/src/drivers/speaker.c \
    $(ROOT_DIR)/kernel/src/drivers/tty.c \
    $(ROOT_DIR)/kernel/src/drivers/drm.c \
    $(ROOT_DIR)/kernel/src/drivers/evdev.c \
    $(ROOT_DIR)/kernel/src/drivers/acpi.c \
    $(ROOT_DIR)/kernel/src/drivers/ioapic.c \
    $(ROOT_DIR)/kernel/src/drivers/usb/usb.c \
    $(ROOT_DIR)/kernel/src/drivers/usb/xhci.c \
    $(ROOT_DIR)/kernel/src/drivers/usb/ehci.c \
    $(ROOT_DIR)/kernel/src/drivers/usb/hid.c \
    $(ROOT_DIR)/kernel/src/drivers/rtc.c \
    $(ROOT_DIR)/kernel/src/drivers/ata.c \
    $(ROOT_DIR)/kernel/src/drivers/ahci.c \
    $(ROOT_DIR)/kernel/src/drivers/pci.c \
    $(ROOT_DIR)/kernel/src/drivers/e1000.c \
    $(ROOT_DIR)/kernel/src/drivers/virtio_net.c \
    $(ROOT_DIR)/kernel/src/drivers/rtl8139.c \
    $(ROOT_DIR)/kernel/src/net/net.c \
    $(ROOT_DIR)/kernel/src/net/netif.c \
    $(ROOT_DIR)/kernel/src/net/loopback.c \
    $(ROOT_DIR)/kernel/src/net/arp.c \
    $(ROOT_DIR)/kernel/src/net/ipv4.c \
    $(ROOT_DIR)/kernel/src/net/icmp.c \
    $(ROOT_DIR)/kernel/src/net/udp.c \
    $(ROOT_DIR)/kernel/src/net/tcp.c \
    $(ROOT_DIR)/kernel/src/net/socket.c \
    $(ROOT_DIR)/kernel/arch/x86_64/gdt.c \
    $(ROOT_DIR)/kernel/arch/x86_64/idt.c \
    $(ROOT_DIR)/kernel/arch/x86_64/pic.c \
    $(ROOT_DIR)/kernel/arch/x86_64/pit.c \
    $(ROOT_DIR)/kernel/arch/x86_64/syscall_arch.c

KERNEL_ASM_SRCS := \
    $(ROOT_DIR)/kernel/arch/x86_64/isr.asm \
    $(ROOT_DIR)/kernel/arch/x86_64/context.asm \
    $(ROOT_DIR)/kernel/arch/x86_64/syscall_entry.asm

KERNEL_C_OBJS   := $(patsubst $(ROOT_DIR)/kernel/%.c, $(BUILD_DIR)/kernel/%.o, $(KERNEL_C_SRCS))
KERNEL_ASM_OBJS := $(patsubst $(ROOT_DIR)/kernel/%.asm, $(BUILD_DIR)/kernel/%.o, $(KERNEL_ASM_SRCS))
KERNEL_ALL_OBJS := $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS)

GEN_PANIC_IMAGE := $(BUILD_DIR)/include/drivers/panic_image.h

.PHONY: all kernel

all: kernel

kernel: $(KERNEL_ELF)

$(GEN_PANIC_IMAGE): $(ROOT_DIR)/artwork/szpont-detected.jpg $(ROOT_DIR)/scripts/generate_panic_image.py
	@mkdir -p $(dir $@)
	@python3 $(ROOT_DIR)/scripts/generate_panic_image.py $< $@

$(BUILD_DIR)/kernel/src/drivers/framebuffer.o: $(GEN_PANIC_IMAGE)
$(BUILD_DIR)/kernel/src/panic.o: $(GEN_PANIC_IMAGE)

$(BUILD_DIR)/kernel/%.o: $(ROOT_DIR)/kernel/%.c
	@mkdir -p $(dir $@)
	@echo "  [CC]  $<"
	@$(CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/kernel/$*.d -c $< -o $@

$(BUILD_DIR)/kernel/%.o: $(ROOT_DIR)/kernel/%.asm
	@mkdir -p $(dir $@)
	@echo "  [ASM] $<"
	@$(NASM) $(ASMFLAGS) $< -o $@

$(KERNEL_ELF): $(KERNEL_ALL_OBJS) $(ROOT_DIR)/kernel/linker.ld
	@mkdir -p $(BUILD_DIR)
	@echo "  [LD]  $@"
	@$(LD) $(LDFLAGS) $(KERNEL_ALL_OBJS) -o $@

KERNEL_DEPS := $(KERNEL_C_OBJS:.o=.d)
-include $(KERNEL_DEPS)
