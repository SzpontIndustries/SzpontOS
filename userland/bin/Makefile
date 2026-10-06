# SzpontOS Userland Core Utilities & Applications Makefile
ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/config.mk

# Essential Rescue / Single-User Boot Binaries (/bin)
BIN_PROGS := \
    $(ROOTFS_DIR)/bin/cat \
    $(ROOTFS_DIR)/bin/chmod \
    $(ROOTFS_DIR)/bin/chown \
    $(ROOTFS_DIR)/bin/date \
    $(ROOTFS_DIR)/bin/df \
    $(ROOTFS_DIR)/bin/dmesg \
    $(ROOTFS_DIR)/bin/hostname \
    $(ROOTFS_DIR)/bin/ifconfig \
    $(ROOTFS_DIR)/bin/insmod \
    $(ROOTFS_DIR)/bin/kill \
    $(ROOTFS_DIR)/bin/ls \
    $(ROOTFS_DIR)/bin/lsmod \
    $(ROOTFS_DIR)/bin/mkdir \
    $(ROOTFS_DIR)/bin/modinfo \
    $(ROOTFS_DIR)/bin/mount \
    $(ROOTFS_DIR)/bin/ping \
    $(ROOTFS_DIR)/bin/poweroff \
    $(ROOTFS_DIR)/bin/ps \
    $(ROOTFS_DIR)/bin/reboot \
    $(ROOTFS_DIR)/bin/rm \
    $(ROOTFS_DIR)/bin/rmmod \
    $(ROOTFS_DIR)/bin/shutdown \
    $(ROOTFS_DIR)/bin/sleep \
    $(ROOTFS_DIR)/bin/su \
    $(ROOTFS_DIR)/bin/sync \
    $(ROOTFS_DIR)/bin/sysctl \
    $(ROOTFS_DIR)/bin/touch \
    $(ROOTFS_DIR)/bin/uname

# Extended User Applications, Tools, Network & Graphical Desktop (/usr/bin)
USR_BIN_PROGS := \
    $(ROOTFS_DIR)/usr/bin/clear \
    $(ROOTFS_DIR)/usr/bin/clock \
    $(ROOTFS_DIR)/usr/bin/donut \
    $(ROOTFS_DIR)/usr/bin/env \
    $(ROOTFS_DIR)/usr/bin/find \
    $(ROOTFS_DIR)/usr/bin/free \
    $(ROOTFS_DIR)/usr/bin/grep \
    $(ROOTFS_DIR)/usr/bin/groupadd \
    $(ROOTFS_DIR)/usr/bin/head \
    $(ROOTFS_DIR)/usr/bin/hello \
    $(ROOTFS_DIR)/usr/bin/host \
    $(ROOTFS_DIR)/usr/bin/httpd \
    $(ROOTFS_DIR)/usr/bin/httpget \
    $(ROOTFS_DIR)/usr/bin/id \
    $(ROOTFS_DIR)/usr/bin/killall \
    $(ROOTFS_DIR)/usr/bin/lspci \
    $(ROOTFS_DIR)/usr/bin/lsusb \
    $(ROOTFS_DIR)/usr/bin/makaljer \
    $(ROOTFS_DIR)/usr/bin/nc \
    $(ROOTFS_DIR)/usr/bin/startx \
    $(ROOTFS_DIR)/usr/bin/sudo \
    $(ROOTFS_DIR)/usr/bin/szpontdetected \
    $(ROOTFS_DIR)/usr/bin/szponterm \
    $(ROOTFS_DIR)/usr/bin/szpontui_demo \
    $(ROOTFS_DIR)/usr/bin/tail \
    $(ROOTFS_DIR)/usr/bin/top \
    $(ROOTFS_DIR)/usr/bin/uptime \
    $(ROOTFS_DIR)/usr/bin/useradd \
    $(ROOTFS_DIR)/usr/bin/userdel \
    $(ROOTFS_DIR)/usr/bin/wc \
    $(ROOTFS_DIR)/usr/bin/whoami

TEST_PROGS := \
    $(ROOTFS_DIR)/usr/tbin/dltest \
    $(ROOTFS_DIR)/usr/tbin/mathtest \
    $(ROOTFS_DIR)/usr/tbin/threadtest \
    $(ROOTFS_DIR)/usr/tbin/tuitest \
    $(ROOTFS_DIR)/usr/tbin/drmtest \
    $(ROOTFS_DIR)/usr/tbin/randtest \
    $(ROOTFS_DIR)/usr/tbin/tmpfstest \
    $(ROOTFS_DIR)/usr/tbin/shmtest \
    $(ROOTFS_DIR)/usr/tbin/ptytest \
    $(ROOTFS_DIR)/usr/tbin/kqueuetest \
    $(ROOTFS_DIR)/usr/tbin/mousetest \
    $(ROOTFS_DIR)/usr/tbin/unixtest \
    $(ROOTFS_DIR)/usr/tbin/gittest \
    $(ROOTFS_DIR)/usr/tbin/curltest \
    $(ROOTFS_DIR)/usr/tbin/mesadrmtest \
    $(ROOTFS_DIR)/usr/tbin/cpptest \
    $(ROOTFS_DIR)/usr/tbin/gltriangle \
    $(ROOTFS_DIR)/usr/tbin/glxgears \
    $(ROOTFS_DIR)/usr/tbin/epolltest \
    $(ROOTFS_DIR)/usr/tbin/eventfdtest \
    $(ROOTFS_DIR)/usr/tbin/inotifytest \
    $(ROOTFS_DIR)/usr/tbin/timerfdtest \
    $(ROOTFS_DIR)/usr/tbin/signalfdtest \
    $(ROOTFS_DIR)/usr/tbin/compattest \
    $(ROOTFS_DIR)/usr/tbin/sysfstest

.PHONY: all progs symlinks

# NOTE (single-shot fix #2): keep compiled objects. Without this, make treats
# build/userland/bin/*.o as intermediates (reachable only via chained pattern
# rules) and DELETES them after linking, so every touch of one .c recompiled
# AND relinked the whole directory on the next run.
.SECONDARY:

all: progs symlinks

# NOTE (single-shot fix): object rules below list $(BUILD_DIR)/userland/bin as
# an order-only prerequisite, but no rule created it - clean builds died with
# "No rule to make target `build/userland/bin'". Every other userland
# sub-makefile already has its equivalent mkdir rule.
$(BUILD_DIR)/userland/bin:
	@mkdir -p $@

progs: $(BIN_PROGS) $(USR_BIN_PROGS) $(TEST_PROGS)

symlinks: $(BIN_PROGS) $(USR_BIN_PROGS)
	@mkdir -p $(ROOTFS_DIR)/usr/bin $(ROOTFS_DIR)/bin
	@ln -sf /usr/bin/killall $(ROOTFS_DIR)/bin/killall
	@ln -sf /bin/kill $(ROOTFS_DIR)/usr/bin/kill
	@ln -sf /usr/bin/sudo $(ROOTFS_DIR)/bin/sudo
	@ln -sf /bin/su $(ROOTFS_DIR)/usr/bin/su
	@ln -sf /usr/bin/env $(ROOTFS_DIR)/bin/env
	@ln -sf /usr/bin/startx $(ROOTFS_DIR)/bin/startx
	@ln -sf /usr/bin/szpontdesktop $(ROOTFS_DIR)/bin/szpontdesktop
	@ln -sf /usr/bin/szponterm $(ROOTFS_DIR)/bin/szponterm
	@ln -sf /usr/bin/makaljer $(ROOTFS_DIR)/bin/makaljer
	@ln -sf /usr/bin/szpontdetected $(ROOTFS_DIR)/bin/szpontdetected
	@chmod 4755 $(ROOTFS_DIR)/usr/bin/sudo $(ROOTFS_DIR)/bin/su 2>/dev/null || true

# Compile standard utility objects
$(BUILD_DIR)/userland/bin/%.o: $(ROOT_DIR)/userland/bin/%.c | $(SYSROOT_STAMP) $(BUILD_DIR)/userland/bin
	@mkdir -p $(BUILD_DIR)/userland/bin
	@echo "  [CC]  $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/$*.d -isystem $(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include -c $< -o $@

$(ROOTFS_DIR)/bin $(ROOTFS_DIR)/usr/bin $(ROOTFS_DIR)/usr/tbin:
	@mkdir -p $@

# Link standard dynamic binaries in /bin
$(ROOTFS_DIR)/bin/%: $(BUILD_DIR)/userland/bin/%.o $(CRT0_O) | $(SYSROOT_STAMP) $(ROOTFS_DIR)/bin $(LIBC_SO) $(LIBM_SO)
	@mkdir -p $(ROOTFS_DIR)/bin
	@rm -f $@
	@echo "  [LD]  $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $< -L$(ROOTFS_DIR)/lib -L$(ROOTFS_DIR)/usr/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -lm -lc -o $@
	@chmod +x $@

# Link standard dynamic binaries in /usr/bin
$(ROOTFS_DIR)/usr/bin/%: $(BUILD_DIR)/userland/bin/%.o $(CRT0_O) | $(SYSROOT_STAMP) $(ROOTFS_DIR)/usr/bin $(LIBC_SO) $(LIBM_SO)
	@mkdir -p $(ROOTFS_DIR)/usr/bin
	@rm -f $@
	@echo "  [LD]  $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $< -L$(ROOTFS_DIR)/lib -L$(ROOTFS_DIR)/usr/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -lm -lc -o $@
	@chmod +x $@

# Link standard test binaries
$(ROOTFS_DIR)/usr/tbin/%: $(BUILD_DIR)/userland/bin/%.o $(CRT0_O) | $(SYSROOT_STAMP) $(ROOTFS_DIR)/usr/tbin $(LIBC_SO) $(LIBM_SO)
	@mkdir -p $(ROOTFS_DIR)/usr/tbin
	@rm -f $@
	@echo "  [LD]  $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $< -L$(ROOTFS_DIR)/lib -L$(ROOTFS_DIR)/usr/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -lm -lc -o $@
	@chmod +x $@

# Build donut
$(BUILD_DIR)/userland/bin/donut.o: $(ROOT_DIR)/userland/bin/donut.c | $(SYSROOT_STAMP) $(BUILD_DIR)/userland/bin
	@mkdir -p $(BUILD_DIR)/userland/bin
	@echo "  [CC-DYN] $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/donut.d -c $< -o $@

$(ROOTFS_DIR)/usr/bin/donut: $(BUILD_DIR)/userland/bin/donut.o $(CRT0_O) | $(SYSROOT_STAMP) $(ROOTFS_DIR)/usr/bin $(LIBC_SO) $(LIBM_SO)
	@mkdir -p $(ROOTFS_DIR)/usr/bin
	@rm -f $@
	@echo "  [LD-DYN] $@"
	@$(LD) $(USER_LDFLAGS) -pie $(CRT0_O) $< $(LIBM_SO) $(LIBC_SO) -o $@

# Native X11 Terminal Emulator (szponterm)
$(BUILD_DIR)/userland/bin/szponterm.o: $(ROOT_DIR)/userland/bin/szponterm.c | $(SYSROOT_STAMP) $(BUILD_DIR)/userland/bin
	@mkdir -p $(BUILD_DIR)/userland/bin
	@echo "  [CC-SZPONTERM] $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/szponterm.d -isystem $(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include -c $< -o $@

$(ROOTFS_DIR)/usr/bin/szponterm: $(BUILD_DIR)/userland/bin/szponterm.o $(CRT0_O) | $(SYSROOT_STAMP) $(ROOTFS_DIR)/usr/bin $(LIBC_SO) $(LIBM_SO) $(ROOTFS_DIR)/usr/lib/libX11.so
	@mkdir -p $(ROOTFS_DIR)/usr/bin
	@rm -f $@
	@echo "  [LD-SZPONTERM] $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $< -L$(ROOTFS_DIR)/lib -L$(ROOTFS_DIR)/usr/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -lX11 -lxcb -lXau -lXdmcp -lm -lc -o $@
	@chmod +x $@

# Native X11 Artwork Viewer (makaljer)
$(BUILD_DIR)/userland/bin/makaljer.o: $(ROOT_DIR)/userland/bin/makaljer.c | $(SYSROOT_STAMP) $(BUILD_DIR)/userland/bin
	@mkdir -p $(BUILD_DIR)/userland/bin
	@echo "  [CC-MAKALJER] $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/makaljer.d -isystem $(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include -I$(ROOT_DIR)/third_party/stb -c $< -o $@

$(ROOTFS_DIR)/usr/bin/makaljer: $(BUILD_DIR)/userland/bin/makaljer.o $(CRT0_O) | $(SYSROOT_STAMP) $(ROOTFS_DIR)/usr/bin $(LIBC_SO) $(LIBM_SO) $(ROOTFS_DIR)/usr/lib/libX11.so $(ROOTFS_DIR)/usr/lib/libXext.so
	@mkdir -p $(ROOTFS_DIR)/usr/bin
	@rm -f $@
	@echo "  [LD-MAKALJER] $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $< -L$(ROOTFS_DIR)/lib -L$(ROOTFS_DIR)/usr/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -lXext -lX11 -lxcb -lXau -lXdmcp -lm -lc -o $@
	@chmod +x $@

# Native X11 Artwork Viewer (szpontdetected)
$(BUILD_DIR)/userland/bin/szpontdetected.o: $(ROOT_DIR)/userland/bin/szpontdetected.c | $(SYSROOT_STAMP) $(BUILD_DIR)/userland/bin
	@mkdir -p $(BUILD_DIR)/userland/bin
	@echo "  [CC-SZPONTDETECTED] $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/szpontdetected.d -isystem $(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include -I$(ROOT_DIR)/third_party/stb -c $< -o $@

$(ROOTFS_DIR)/usr/bin/szpontdetected: $(BUILD_DIR)/userland/bin/szpontdetected.o $(CRT0_O) | $(SYSROOT_STAMP) $(ROOTFS_DIR)/usr/bin $(LIBC_SO) $(LIBM_SO) $(ROOTFS_DIR)/usr/lib/libX11.so $(ROOTFS_DIR)/usr/lib/libXext.so
	@mkdir -p $(ROOTFS_DIR)/usr/bin
	@rm -f $@
	@echo "  [LD-SZPONTDETECTED] $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $< -L$(ROOTFS_DIR)/lib -L$(ROOTFS_DIR)/usr/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -lXext -lX11 -lxcb -lXau -lXdmcp -lm -lc -o $@
	@chmod +x $@

# X11 Session Launcher (startx)
$(BUILD_DIR)/userland/bin/startx.o: $(ROOT_DIR)/userland/bin/startx.c | $(SYSROOT_STAMP) $(BUILD_DIR)/userland/bin
	@mkdir -p $(BUILD_DIR)/userland/bin
	@echo "  [CC-STARTX] $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/startx.d -c $< -o $@

$(ROOTFS_DIR)/usr/bin/startx: $(BUILD_DIR)/userland/bin/startx.o $(CRT0_O) | $(SYSROOT_STAMP) $(ROOTFS_DIR)/usr/bin $(LIBC_SO)
	@mkdir -p $(ROOTFS_DIR)/usr/bin
	@rm -f $@
	@echo "  [LD-STARTX] $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $< -L$(ROOTFS_DIR)/lib -L$(ROOTFS_DIR)/usr/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -lc -o $@
	@chmod +x $@

# cURL Diagnostic Test Tool
$(BUILD_DIR)/userland/bin/curltest.o: $(ROOT_DIR)/userland/bin/curltest.c | $(SYSROOT_STAMP) $(BUILD_DIR)/userland/bin
	@mkdir -p $(BUILD_DIR)/userland/bin
	@echo "  [CC-CURLTEST] $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/curltest.d -isystem $(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include -c $< -o $@

$(ROOTFS_DIR)/usr/tbin/curltest: $(BUILD_DIR)/userland/bin/curltest.o $(CRT0_O) | $(SYSROOT_STAMP) $(ROOTFS_DIR)/usr/tbin $(LIBC_SO) $(ROOTFS_DIR)/usr/lib/libcurl.so $(ROOTFS_DIR)/usr/lib/libz.so
	@mkdir -p $(ROOTFS_DIR)/usr/tbin
	@echo "  [LD-CURLTEST] $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $< -L$(ROOTFS_DIR)/lib -L$(ROOTFS_DIR)/usr/lib -Wl,-rpath-link=$(ROOTFS_DIR)/usr/lib -lcurl -lssl -lcrypto -lz -lm -lc -o $@
	@chmod +x $@

# Native DRM test utilities linked against libdrm.so
$(BUILD_DIR)/userland/bin/drmtest.o: $(ROOT_DIR)/userland/bin/drmtest.c | $(SYSROOT_STAMP) $(BUILD_DIR)/userland/bin
	@mkdir -p $(BUILD_DIR)/userland/bin
	@echo "  [CC]  $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/drmtest.d -isystem $(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include/libdrm -c $< -o $@

$(ROOTFS_DIR)/usr/tbin/drmtest: $(BUILD_DIR)/userland/bin/drmtest.o $(CRT0_O) | $(SYSROOT_STAMP) $(ROOTFS_DIR)/usr/tbin $(LIBC_SO) $(LIBM_SO) $(ROOTFS_DIR)/usr/lib/libdrm.so
	@mkdir -p $(ROOTFS_DIR)/usr/tbin
	@echo "  [LD]  $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $< -L$(ROOTFS_DIR)/lib -L$(ROOTFS_DIR)/usr/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -ldrm -lm -lc -o $@
	@chmod +x $@

$(BUILD_DIR)/userland/bin/mesadrmtest.o: $(ROOT_DIR)/userland/bin/mesadrmtest.c | $(SYSROOT_STAMP) $(BUILD_DIR)/userland/bin
	@mkdir -p $(BUILD_DIR)/userland/bin
	@echo "  [CC]  $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/mesadrmtest.d -isystem $(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include/libdrm -c $< -o $@

$(ROOTFS_DIR)/usr/tbin/mesadrmtest: $(BUILD_DIR)/userland/bin/mesadrmtest.o $(CRT0_O) | $(SYSROOT_STAMP) $(ROOTFS_DIR)/usr/tbin $(LIBC_SO) $(LIBM_SO) $(ROOTFS_DIR)/usr/lib/libdrm.so
	@mkdir -p $(ROOTFS_DIR)/usr/tbin
	@echo "  [LD]  $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $< -L$(ROOTFS_DIR)/lib -L$(ROOTFS_DIR)/usr/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -ldrm -lm -lc -o $@
	@chmod +x $@

# Native C++ Validation Suite linked against libstdc++.so, libm.so, libc.so
$(BUILD_DIR)/userland/bin/cpptest.o: $(ROOT_DIR)/userland/bin/cpptest.cpp | $(SYSROOT_STAMP) $(BUILD_DIR)/userland/bin
	@mkdir -p $(BUILD_DIR)/userland/bin
	@echo "  [CXX] $<"
	@$(CXX) $(USER_CXXFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/cpptest.d -c $< -o $@

$(ROOTFS_DIR)/usr/tbin/cpptest: $(BUILD_DIR)/userland/bin/cpptest.o $(CRT0_O) $(CRTI_O) $(CRTN_O) | $(SYSROOT_STAMP) $(ROOTFS_DIR)/usr/tbin $(LIBC_SO) $(LIBM_SO) $(LIBSTDCXX_SO)
	@mkdir -p $(ROOTFS_DIR)/usr/tbin
	@echo "  [LD]  $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $(CRTI_O) $$($(CC) -print-file-name=crtbegin.o) $< $$($(CC) -print-file-name=crtend.o) $(CRTN_O) -L$(ROOTFS_DIR)/lib -L$(ROOTFS_DIR)/usr/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -lstdc++ -lm -lc -o $@
	@chmod +x $@

# Native SzpontUI C++ Showcase Application
$(BUILD_DIR)/userland/bin/szpontui_demo.o: $(ROOT_DIR)/userland/bin/szpontui_demo.cpp | $(SYSROOT_STAMP) $(BUILD_DIR)/userland/bin
	@mkdir -p $(BUILD_DIR)/userland/bin
	@echo "  [CXX-SZPONTUI-DEMO] $<"
	@$(CXX) $(USER_CXXFLAGS) -isystem $(SYSROOT_DIR)/usr/include/SzpontUI -I$(SYSROOT_DIR)/usr/include/SzpontUI -isystem $(SYSROOT_DIR)/usr/include/freetype2 -isystem $(SYSROOT_DIR)/usr/include/harfbuzz -isystem $(SYSROOT_DIR)/usr/include/pixman-1 -MMD -MP -MF $(BUILD_DIR)/userland/bin/szpontui_demo.d -c $< -o $@

$(ROOTFS_DIR)/usr/bin/szpontui_demo: $(BUILD_DIR)/userland/bin/szpontui_demo.o $(CRT0_O) $(CRTI_O) $(CRTN_O) | $(SYSROOT_STAMP) $(ROOTFS_DIR)/usr/bin $(LIBC_SO) $(LIBM_SO) $(LIBSTDCXX_SO) $(ROOTFS_DIR)/usr/lib/libszpontui.so
	@mkdir -p $(ROOTFS_DIR)/usr/bin
	@rm -f $@
	@echo "  [LD-SZPONTUI-DEMO] $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $(CRTI_O) $$($(CC) -print-file-name=crtbegin.o) $< $$($(CC) -print-file-name=crtend.o) $(CRTN_O) \
		-L$(ROOTFS_DIR)/lib -L$(ROOTFS_DIR)/usr/lib -L$(abspath $(SYSROOT_DIR))/usr/lib \
		-Wl,-rpath-link=$(ROOTFS_DIR)/usr/lib \
		-lszpontui -lharfbuzz -lfreetype -lz -lpixman-1 -lX11 -lXext -lxcb -lXau -lXdmcp -lstdc++ -lm -lc -o $@
	@chmod +x $@


# Native OpenGL / EGL smoke test linked against libEGL.so, libGLESv2.so, libm.so, libc.so
$(BUILD_DIR)/userland/bin/gltriangle.o: $(ROOT_DIR)/userland/bin/gltriangle.c | $(SYSROOT_STAMP) $(BUILD_DIR)/userland/bin
	@mkdir -p $(BUILD_DIR)/userland/bin
	@echo "  [CC]  $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/gltriangle.d -isystem $(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include -c $< -o $@

$(ROOTFS_DIR)/usr/tbin/gltriangle: $(BUILD_DIR)/userland/bin/gltriangle.o $(CRT0_O) | $(SYSROOT_STAMP) $(ROOTFS_DIR)/usr/tbin $(LIBC_SO) $(LIBM_SO)
	@mkdir -p $(ROOTFS_DIR)/usr/tbin
	@echo "  [LD]  $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $< -L$(ROOTFS_DIR)/lib -L$(ROOTFS_DIR)/usr/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -lEGL -lGLESv2 -lm -lc -Wl,--allow-shlib-undefined -o $@
	@chmod +x $@

# Native GLX Gears demo linked against libGL.so, libX11.so, libm.so, libc.so
$(BUILD_DIR)/userland/bin/glxgears.o: $(ROOT_DIR)/userland/bin/glxgears.c | $(SYSROOT_STAMP) $(BUILD_DIR)/userland/bin
	@mkdir -p $(BUILD_DIR)/userland/bin
	@echo "  [CC]  $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/userland/bin/glxgears.d -isystem $(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include -c $< -o $@

$(ROOTFS_DIR)/usr/tbin/glxgears: $(BUILD_DIR)/userland/bin/glxgears.o $(CRT0_O) | $(SYSROOT_STAMP) $(ROOTFS_DIR)/usr/tbin $(LIBC_SO) $(LIBM_SO) $(ROOTFS_DIR)/usr/lib/libGL.so $(ROOTFS_DIR)/usr/lib/libX11.so
	@mkdir -p $(ROOTFS_DIR)/usr/tbin
	@echo "  [LD]  $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $< -L$(ROOTFS_DIR)/lib -L$(ROOTFS_DIR)/usr/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -lGL -lX11 -lxcb -lm -lc -Wl,--allow-shlib-undefined -o $@
	@chmod +x $@

-include $(wildcard $(BUILD_DIR)/userland/bin/*.d)
