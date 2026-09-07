# SzpontX11 Native X11 Graphical Display Server Makefile
ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/config.mk

TARGET := $(ROOTFS_DIR)/bin/SzpontX11
HDRS   := $(wildcard $(ROOT_DIR)/userland/xserver/include/*.h)
SRCS   := $(wildcard $(ROOT_DIR)/userland/xserver/src/*.c)
OBJS   := $(patsubst $(ROOT_DIR)/userland/xserver/src/%.c,$(BUILD_DIR)/userland/xserver/%.o,$(SRCS))

.PHONY: all xserver

all: xserver

xserver: $(TARGET)

$(BUILD_DIR)/userland/xserver/%.o: $(ROOT_DIR)/userland/xserver/src/%.c $(HDRS) | $(BUILD_DIR)/userland/xserver
	@echo "  [CC-SZPONTX11] $<"
	@$(CC) $(USER_CFLAGS) -I$(ROOT_DIR)/userland/xserver/include -isystem $(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include/libdrm -c $< -o $@

$(BUILD_DIR)/userland/xserver:
	@mkdir -p $@

$(TARGET): $(OBJS) $(CRT0_O) $(LIBC_SO) $(LIBM_SO) | $(ROOTFS_DIR)
	@mkdir -p $(ROOTFS_DIR)/bin
	@echo "  [LD-SZPONTX11] $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $(OBJS) \
	    -L$(ROOTFS_DIR)/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -ldrm -lpixman-1 -lm -lc -o $@
	@chmod +x $@
	@ln -sf SzpontX11 $(ROOTFS_DIR)/bin/Xorg
	@ln -sf SzpontX11 $(ROOTFS_DIR)/bin/X

$(ROOTFS_DIR)/bin/Xorg: $(TARGET)
$(ROOTFS_DIR)/bin/X: $(TARGET)
