# SzpontOS Userland Core Utilities & Applications Makefile
ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/config.mk

CORE_PROGS := \
    $(ROOTFS_DIR)/bin/hello \
    $(ROOTFS_DIR)/bin/cat \
    $(ROOTFS_DIR)/bin/id \
    $(ROOTFS_DIR)/bin/whoami \
    $(ROOTFS_DIR)/bin/useradd \
    $(ROOTFS_DIR)/bin/userdel \
    $(ROOTFS_DIR)/bin/groupadd \
    $(ROOTFS_DIR)/bin/su \
    $(ROOTFS_DIR)/bin/chmod \
    $(ROOTFS_DIR)/bin/chown \
    $(ROOTFS_DIR)/bin/df \
    $(ROOTFS_DIR)/bin/ps \
    $(ROOTFS_DIR)/bin/free \
    $(ROOTFS_DIR)/bin/uptime \
    $(ROOTFS_DIR)/bin/date \
    $(ROOTFS_DIR)/bin/clock \
    $(ROOTFS_DIR)/bin/mkdir \
    $(ROOTFS_DIR)/bin/touch \
    $(ROOTFS_DIR)/bin/head \
    $(ROOTFS_DIR)/bin/tail \
    $(ROOTFS_DIR)/bin/wc \
    $(ROOTFS_DIR)/bin/clear \
    $(ROOTFS_DIR)/bin/mount \
    $(ROOTFS_DIR)/bin/dltest \
    $(ROOTFS_DIR)/bin/mathtest \
    $(ROOTFS_DIR)/bin/threadtest \
    $(ROOTFS_DIR)/bin/insmod \
    $(ROOTFS_DIR)/bin/rmmod \
    $(ROOTFS_DIR)/bin/lsmod \
    $(ROOTFS_DIR)/bin/modinfo \
    $(ROOTFS_DIR)/bin/rm \
    $(ROOTFS_DIR)/bin/hostname \
    $(ROOTFS_DIR)/bin/uname \
    $(ROOTFS_DIR)/bin/tuitest \
    $(ROOTFS_DIR)/bin/drmtest \
    $(ROOTFS_DIR)/bin/ifconfig \
    $(ROOTFS_DIR)/bin/ping \
    $(ROOTFS_DIR)/bin/sleep \
    $(ROOTFS_DIR)/bin/nc \
    $(ROOTFS_DIR)/bin/httpd \
    $(ROOTFS_DIR)/bin/sysctl \
    $(ROOTFS_DIR)/bin/kill \
    $(ROOTFS_DIR)/bin/killall \
    $(ROOTFS_DIR)/bin/dmesg \
    $(ROOTFS_DIR)/bin/randtest \
    $(ROOTFS_DIR)/bin/tmpfstest \
    $(ROOTFS_DIR)/bin/shmtest \
    $(ROOTFS_DIR)/bin/ptytest \
    $(ROOTFS_DIR)/bin/kqueuetest \
    $(ROOTFS_DIR)/bin/grep \
    $(ROOTFS_DIR)/bin/find \
    $(ROOTFS_DIR)/bin/top \
    $(ROOTFS_DIR)/bin/reboot \
    $(ROOTFS_DIR)/bin/host \
    $(ROOTFS_DIR)/bin/httpget \
    $(ROOTFS_DIR)/bin/shutdown \
    $(ROOTFS_DIR)/bin/poweroff \
    $(ROOTFS_DIR)/bin/sync \
    $(ROOTFS_DIR)/bin/ls \
    $(ROOTFS_DIR)/bin/lspci \
    $(ROOTFS_DIR)/bin/lsusb \
    $(ROOTFS_DIR)/bin/donut \
    $(ROOTFS_DIR)/bin/mousetest \
    $(ROOTFS_DIR)/bin/unixtest \
    $(ROOTFS_DIR)/bin/gittest \
    $(ROOTFS_DIR)/bin/startx \
    $(ROOTFS_DIR)/bin/szpontdesktop \
    $(ROOTFS_DIR)/bin/szponterm \
    $(ROOTFS_DIR)/bin/curltest \
    $(ROOTFS_DIR)/bin/mesadrmtest

.PHONY: all progs

all: progs

progs: $(CORE_PROGS)

# Standard dynamic binaries linked against libc.so and libm.so
$(ROOTFS_DIR)/bin/%: $(ROOT_DIR)/userland/bin/%.c $(CRT0_O) $(LIBC_SO) $(LIBM_SO) | $(SYSROOT_STAMP) $(ROOTFS_DIR)
	@mkdir -p $(ROOTFS_DIR)/bin $(BUILD_DIR)/userland/bin
	@echo "  [CC]  $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/$*.d -isystem $(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include -c $< -o $(BUILD_DIR)/userland/bin/$*.o
	@echo "  [LD]  $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $(BUILD_DIR)/userland/bin/$*.o -L$(ROOTFS_DIR)/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -lm -lc -o $@
	@chmod +x $@

# Build donut (dynamically linked against libc.so and libm.so)
$(ROOTFS_DIR)/bin/donut: $(ROOT_DIR)/userland/bin/donut.c $(CRT0_O) $(LIBC_SO) $(LIBM_SO) | $(SYSROOT_STAMP) $(ROOTFS_DIR)
	@mkdir -p $(ROOTFS_DIR)/bin $(BUILD_DIR)/userland/bin
	@echo "  [CC-DYN] $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/donut.d -c $< -o $(BUILD_DIR)/userland/bin/donut.o
	@echo "  [LD-DYN] $@"
	@$(LD) $(USER_LDFLAGS) -pie $(CRT0_O) $(BUILD_DIR)/userland/bin/donut.o $(LIBM_SO) $(LIBC_SO) -o $@

# X11 Desktop Environment (szpontdesktop)
$(ROOTFS_DIR)/bin/szpontdesktop: $(ROOT_DIR)/userland/bin/szpontdesktop.c $(CRT0_O) $(LIBC_SO) $(LIBM_SO) $(ROOTFS_DIR)/lib/libX11.so $(ROOTFS_DIR)/lib/libXext.so | $(SYSROOT_STAMP) $(ROOTFS_DIR)
	@mkdir -p $(ROOTFS_DIR)/bin $(BUILD_DIR)/userland/bin
	@echo "  [CC-SZPONTDESKTOP] $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/szpontdesktop.d -isystem $(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include -I$(ROOT_DIR)/third_party/stb -c $< -o $(BUILD_DIR)/userland/bin/szpontdesktop.o
	@echo "  [LD-SZPONTDESKTOP] $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $(BUILD_DIR)/userland/bin/szpontdesktop.o -L$(ROOTFS_DIR)/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -lXext -lX11 -lxcb -lXau -lXdmcp -lm -lc -o $@
	@chmod +x $@

# Native X11 Terminal Emulator (szponterm)
$(ROOTFS_DIR)/bin/szponterm: $(ROOT_DIR)/userland/bin/szponterm.c $(CRT0_O) $(LIBC_SO) $(LIBM_SO) $(ROOTFS_DIR)/lib/libX11.so | $(SYSROOT_STAMP) $(ROOTFS_DIR)
	@mkdir -p $(ROOTFS_DIR)/bin $(BUILD_DIR)/userland/bin
	@echo "  [CC-SZPONTERM] $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/szponterm.d -isystem $(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include -c $< -o $(BUILD_DIR)/userland/bin/szponterm.o
	@echo "  [LD-SZPONTERM] $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $(BUILD_DIR)/userland/bin/szponterm.o -L$(ROOTFS_DIR)/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -lX11 -lxcb -lXau -lXdmcp -lm -lc -o $@
	@chmod +x $@

# X11 Session Launcher (startx)
$(ROOTFS_DIR)/bin/startx: $(ROOT_DIR)/userland/bin/startx.c $(CRT0_O) $(LIBC_SO) | $(SYSROOT_STAMP) $(ROOTFS_DIR)
	@mkdir -p $(ROOTFS_DIR)/bin $(BUILD_DIR)/userland/bin
	@echo "  [CC-STARTX] $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/startx.d -c $< -o $(BUILD_DIR)/userland/bin/startx.o
	@echo "  [LD-STARTX] $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $(BUILD_DIR)/userland/bin/startx.o -L$(ROOTFS_DIR)/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -lc -o $@
	@chmod +x $@

# cURL Diagnostic Test Tool
$(ROOTFS_DIR)/bin/curltest: $(ROOT_DIR)/userland/bin/curltest.c $(CRT0_O) $(LIBC_SO) $(ROOTFS_DIR)/lib/libcurl.so | $(SYSROOT_STAMP) $(ROOTFS_DIR)
	@mkdir -p $(ROOTFS_DIR)/bin $(BUILD_DIR)/userland/bin
	@echo "  [CC-CURLTEST] $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/curltest.d -isystem $(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include -c $< -o $(BUILD_DIR)/userland/bin/curltest.o
	@echo "  [LD-CURLTEST] $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $(BUILD_DIR)/userland/bin/curltest.o $(ROOTFS_DIR)/lib/libcurl.so.4 $(ROOTFS_DIR)/lib/libssl.so.4 $(ROOTFS_DIR)/lib/libcrypto.so.4 $(ROOTFS_DIR)/lib/libc.so $(ROOTFS_DIR)/lib/libm.so -o $@
	@chmod +x $@

# Native DRM test utilities linked against libdrm.so
$(ROOTFS_DIR)/bin/drmtest: $(ROOT_DIR)/userland/bin/drmtest.c $(CRT0_O) $(LIBC_SO) $(LIBM_SO) $(ROOTFS_DIR)/lib/libdrm.so | $(SYSROOT_STAMP) $(ROOTFS_DIR)
	@mkdir -p $(ROOTFS_DIR)/bin $(BUILD_DIR)/userland/bin
	@echo "  [CC]  $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/drmtest.d -isystem $(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include/libdrm -c $< -o $(BUILD_DIR)/userland/bin/drmtest.o
	@echo "  [LD]  $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $(BUILD_DIR)/userland/bin/drmtest.o -L$(ROOTFS_DIR)/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -ldrm -lm -lc -o $@
	@chmod +x $@

$(ROOTFS_DIR)/bin/mesadrmtest: $(ROOT_DIR)/userland/bin/mesadrmtest.c $(CRT0_O) $(LIBC_SO) $(LIBM_SO) $(ROOTFS_DIR)/lib/libdrm.so | $(SYSROOT_STAMP) $(ROOTFS_DIR)
	@mkdir -p $(ROOTFS_DIR)/bin $(BUILD_DIR)/userland/bin
	@echo "  [CC]  $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/mesadrmtest.d -isystem $(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include/libdrm -c $< -o $(BUILD_DIR)/userland/bin/mesadrmtest.o
	@echo "  [LD]  $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $(BUILD_DIR)/userland/bin/mesadrmtest.o -L$(ROOTFS_DIR)/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -ldrm -lm -lc -o $@
	@chmod +x $@

-include $(wildcard $(BUILD_DIR)/userland/bin/*.d)

