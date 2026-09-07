# SzpontOS Freestanding C Standard Library (Libc) Makefile
ROOT_DIR ?= $(abspath ..)
include $(ROOT_DIR)/config.mk

LIBC_C_SRCS := \
    $(ROOT_DIR)/libc/src/string/string.c \
    $(ROOT_DIR)/libc/src/stdlib/malloc.c \
    $(ROOT_DIR)/libc/src/stdlib/atexit.c \
    $(ROOT_DIR)/libc/src/stdlib/locale.c \
    $(ROOT_DIR)/libc/src/stdlib/system.c \
    $(ROOT_DIR)/libc/src/stdlib/crypt.c \
    $(ROOT_DIR)/libc/src/stdio/printf.c \
    $(ROOT_DIR)/libc/src/stdio/err.c \
    $(ROOT_DIR)/libc/src/signal/siglist.c \
    $(ROOT_DIR)/libc/src/unistd/unistd.c \
    $(ROOT_DIR)/libc/src/unistd/getrandom.c \
    $(ROOT_DIR)/libc/src/unistd/sysconf.c \
    $(ROOT_DIR)/libc/src/unistd/sendfile.c \
    $(ROOT_DIR)/libc/src/unistd/uio.c \
    $(ROOT_DIR)/libc/src/unistd/daemon.c \
    $(ROOT_DIR)/libc/src/dirent/dirent.c \
    $(ROOT_DIR)/libc/src/pwd/pwd.c \
    $(ROOT_DIR)/libc/src/pwd/shadow.c \
    $(ROOT_DIR)/libc/src/grp/grp.c \
    $(ROOT_DIR)/libc/src/dlfcn/dlfcn.c \
    $(ROOT_DIR)/libc/src/ctype/ctype.c \
    $(ROOT_DIR)/libc/src/time/time.c \
    $(ROOT_DIR)/libc/src/getopt/getopt.c \
    $(ROOT_DIR)/libc/src/mman/mman.c \
    $(ROOT_DIR)/libc/src/regex/regex.c \
    $(ROOT_DIR)/libc/src/math/math.c \
    $(ROOT_DIR)/libc/src/crypto/sha1.c \
    $(ROOT_DIR)/libc/src/pthread/pthread.c \
    $(ROOT_DIR)/libc/src/pthread/mutex.c \
    $(ROOT_DIR)/libc/src/pthread/cond.c \
    $(ROOT_DIR)/libc/src/pthread/sem.c \
    $(ROOT_DIR)/libc/src/termios/termios.c \
    $(ROOT_DIR)/libc/src/wchar/wchar.c \
    $(ROOT_DIR)/libc/src/glob/glob.c \
    $(ROOT_DIR)/libc/src/mntent/mntent.c \
    $(ROOT_DIR)/libc/src/fnmatch/fnmatch.c \
    $(ROOT_DIR)/libc/src/spawn/spawn.c \
    $(ROOT_DIR)/libc/src/poll/poll.c \
    $(ROOT_DIR)/libc/src/uchar/uchar.c \
    $(ROOT_DIR)/libc/src/socket/socket.c \
    $(ROOT_DIR)/libc/src/socket/inet.c \
    $(ROOT_DIR)/libc/src/netdb/netdb.c \
    $(ROOT_DIR)/libc/src/netdb/resolv.c \
    $(ROOT_DIR)/libc/src/net/if.c \
    $(ROOT_DIR)/libc/src/utmp/utmp.c \
    $(ROOT_DIR)/libc/src/sched/sched.c \
    $(ROOT_DIR)/libc/src/pty/pty.c \
    $(ROOT_DIR)/libc/src/shm/shm.c \
    $(ROOT_DIR)/libc/src/arch/x86_64/builtins.c

LIBC_ASM_SRCS := \
    $(ROOT_DIR)/libc/src/arch/x86_64/syscall.asm \
    $(ROOT_DIR)/libc/src/arch/x86_64/setjmp.asm

LIBC_C_OBJS   := $(patsubst $(ROOT_DIR)/libc/%.c, $(BUILD_DIR)/libc/%.o, $(LIBC_C_SRCS))
LIBC_ASM_OBJS := $(patsubst $(ROOT_DIR)/libc/%.asm, $(BUILD_DIR)/libc/%.o, $(LIBC_ASM_SRCS))
LIBC_OBJS     := $(LIBC_C_OBJS) $(LIBC_ASM_OBJS)

.PHONY: all libc sysroot

all: libc sysroot

libc: $(CRT0_O) $(LIBC_A) $(LIBC_SO) $(LIBM_A) $(LIBM_SO) $(LIBDL_A)

# Compile Libc C sources
$(BUILD_DIR)/libc/%.o: $(ROOT_DIR)/libc/%.c
	@mkdir -p $(dir $@)
	@echo "  [CC-LIBC] $<"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/libc/$*.d -c $< -o $@

# Assemble Libc ASM sources
$(BUILD_DIR)/libc/%.o: $(ROOT_DIR)/libc/%.asm
	@mkdir -p $(dir $@)
	@echo "  [ASM-LIBC] $<"
	@$(NASM) $(ASMFLAGS) $< -o $@

# Assemble crt0.o
$(CRT0_O): $(ROOT_DIR)/libc/src/arch/x86_64/crt0.asm
	@mkdir -p $(dir $@)
	@echo "  [ASM] $<"
	@$(NASM) $(ASMFLAGS) $< -o $@

# Build Libc static library
$(LIBC_A): $(LIBC_OBJS)
	@mkdir -p $(dir $@)
	@echo "  [AR]  $@"
	@$(AR) rcs $@ $(LIBC_OBJS)

# Build Libc shared library (.so)
$(LIBC_SO): $(LIBC_OBJS) | $(ROOTFS_DIR)
	@mkdir -p $(ROOTFS_DIR)/lib $(BUILD_DIR)/libc
	@echo "  [LD-SO] $@"
	@$(LD) -shared -soname libc.so $(LIBC_OBJS) -o $@

# Build Libm static library (.a)
$(LIBM_A): $(BUILD_DIR)/libc/src/math/math.o
	@mkdir -p $(dir $@)
	@echo "  [AR]  $@"
	@$(AR) rcs $@ $<

# Build Libm shared library (.so)
$(LIBM_SO): $(BUILD_DIR)/libc/src/math/math.o | $(ROOTFS_DIR)
	@mkdir -p $(ROOTFS_DIR)/lib
	@echo "  [LD-SO] $@"
	@$(LD) -shared -soname libm.so $< -o $@

# Build Libdl static library (.a)
$(LIBDL_A):
	@mkdir -p $(dir $@)
	@echo "  [AR]  $@"
	@$(AR) rcs $@

LIBC_HEADERS := $(shell find $(ROOT_DIR)/libc/include -type f 2>/dev/null)

# Sysroot Target (cross-compilation root for third-party libraries)
$(SYSROOT_STAMP): $(LIBC_A) $(CRT0_O) $(LIBM_A) $(LIBDL_A) $(LIBC_SO) $(LIBM_SO) $(LIBC_HEADERS)
	@mkdir -p $(SYSROOT_DIR)/usr/include $(SYSROOT_DIR)/usr/lib $(SYSROOT_DIR)/usr/lib/pkgconfig $(SYSROOT_DIR)/usr/share/pkgconfig $(SYSROOT_DIR)/usr/share/xcb $(SYSROOT_DIR)/usr/include/libdrm $(SYSROOT_DIR)/usr/include/drm $(SYSROOT_DIR)/usr/include/libxcvt $(SYSROOT_DIR)/usr/include/pixman-1 $(SYSROOT_DIR)/usr/include/X11/Xtrans $(SYSROOT_DIR)/usr/include/X11/extensions
	@cp -R $(ROOT_DIR)/libc/include/. $(SYSROOT_DIR)/usr/include/
	@cp -f $(LIBC_A) $(CRT0_O) $(LIBM_A) $(LIBDL_A) $(SYSROOT_DIR)/usr/lib/ 2>/dev/null || true
	@cp -f $(LIBC_SO) $(LIBM_SO) $(SYSROOT_DIR)/usr/lib/ 2>/dev/null || true
	@cp -R $(ROOT_DIR)/third_party/xorgproto/include/. $(SYSROOT_DIR)/usr/include/ 2>/dev/null || true
	@if [ -f $(ROOT_DIR)/third_party/xorgproto/include/X11/Xpoll.h.in ]; then \
	    sed -e 's|@USE_FDS_BITS@|fds_bits|g' $(ROOT_DIR)/third_party/xorgproto/include/X11/Xpoll.h.in > $(SYSROOT_DIR)/usr/include/X11/Xpoll.h; \
	fi
	@printf '#ifndef _X11_XLIB_CONF_H\n#define _X11_XLIB_CONF_H\n#define XTHREADS 1\n#define _XOPEN_SOURCE 500\n#endif\n' > $(SYSROOT_DIR)/usr/include/X11/XlibConf.h
	@cp -f $(ROOT_DIR)/third_party/xtrans/*.h $(ROOT_DIR)/third_party/xtrans/*.c $(SYSROOT_DIR)/usr/include/X11/Xtrans/ 2>/dev/null || true
	@cp -f $(ROOT_DIR)/third_party/xtrans/*.h $(ROOT_DIR)/third_party/xtrans/*.c $(SYSROOT_DIR)/usr/include/X11/ 2>/dev/null || true
	@if [ -f $(ROOT_DIR)/third_party/xcb-proto/xcb-proto.pc.in ]; then \
	    sed -e 's|@prefix@|$(abspath $(SYSROOT_DIR))/usr|g' \
	        -e 's|@exec_prefix@|$${prefix}|g' \
	        -e 's|@datarootdir@|$${prefix}/share|g' \
	        -e 's|@datadir@|$${datarootdir}|g' \
	        -e 's|@xcbincludedir@|$(abspath $(SYSROOT_DIR))/usr/share/xcb|g' \
	        -e 's|@PACKAGE_VERSION@|1.17.0|g' \
	        $(ROOT_DIR)/third_party/xcb-proto/xcb-proto.pc.in > $(SYSROOT_DIR)/usr/lib/pkgconfig/xcb-proto.pc 2>/dev/null || true; \
	elif [ -f $(ROOT_DIR)/third_party/xcb-proto/xcb-proto.pc ]; then \
	    cp $(ROOT_DIR)/third_party/xcb-proto/xcb-proto.pc $(SYSROOT_DIR)/usr/lib/pkgconfig/ 2>/dev/null || true; \
	fi
	@cp -r $(ROOT_DIR)/third_party/xcb-proto/src/* $(SYSROOT_DIR)/usr/share/xcb/ 2>/dev/null || true
	@cp -f $(ROOT_DIR)/third_party/libxcvt/include/libxcvt/*.h $(SYSROOT_DIR)/usr/include/libxcvt/ 2>/dev/null || true
	@cp -f $(ROOT_DIR)/third_party/pixman/pixman/*.h $(SYSROOT_DIR)/usr/include/pixman-1/ 2>/dev/null || true
	@cp -f $(ROOT_DIR)/third_party/pixman/pixman/*.h $(SYSROOT_DIR)/usr/include/ 2>/dev/null || true
	@for pcin in $(ROOT_DIR)/third_party/xorgproto/*.pc.in; do \
	    pcname=$$(basename $$pcin .in); \
	    sed -e 's|@prefix@|/usr|g' \
	        -e 's|@exec_prefix@|/usr|g' \
	        -e 's|@libdir@|/usr/lib|g' \
	        -e 's|@includedir@|/usr/include|g' \
	        -e 's|@datarootdir@|/usr/share|g' \
	        -e 's|@datadir@|/usr/share|g' \
	        -e 's|@PACKAGE_VERSION@|2024.1|g' \
	        -e 's|@VERSION@|2024.1|g' \
	        $$pcin > $(SYSROOT_DIR)/usr/lib/pkgconfig/$$pcname; \
	done
	@if [ -f $(ROOT_DIR)/third_party/xtrans/xtrans.pc.in ]; then \
	    sed -e 's|@prefix@|/usr|g' \
	        -e 's|@exec_prefix@|$${prefix}|g' \
	        -e 's|@includedir@|$${prefix}/include|g' \
	        -e 's|@datarootdir@|$${prefix}/share|g' \
	        -e 's|@datadir@|$${datarootdir}|g' \
	        -e 's|@PACKAGE_VERSION@|1.5.0|g' \
	        -e 's|@fchown_define@||g' \
	        -e 's|@sticky_bit_define@||g' \
	        $(ROOT_DIR)/third_party/xtrans/xtrans.pc.in > $(SYSROOT_DIR)/usr/lib/pkgconfig/xtrans.pc 2>/dev/null || true; \
	fi
	@if [ -f $(ROOT_DIR)/third_party/util-macros/xorg-macros.pc.in ]; then \
	    sed -e 's|@prefix@|/usr|g' \
	        -e 's|@exec_prefix@|$${prefix}|g' \
	        -e 's|@datarootdir@|$${prefix}/share|g' \
	        -e 's|@datadir@|$${datarootdir}|g' \
	        -e 's|@PACKAGE_VERSION@|1.20.2|g' \
	        $(ROOT_DIR)/third_party/util-macros/xorg-macros.pc.in > $(SYSROOT_DIR)/usr/lib/pkgconfig/xorg-macros.pc 2>/dev/null || true; \
	fi
	@printf "prefix=/usr\nexec_prefix=\$${prefix}\nlibdir=\$${exec_prefix}/lib\nincludedir=\$${prefix}/include\n\nName: pthread-stubs\nDescription: Stubs for pthread functions\nVersion: 0.4\nLibs:\nCflags:\n" > $(SYSROOT_DIR)/usr/lib/pkgconfig/pthread-stubs.pc
	@printf "prefix=/usr\nexec_prefix=\$${prefix}\nlibdir=\$${exec_prefix}/lib\nincludedir=\$${prefix}/include\n\nName: Xau\nDescription: X authorization file management library\nVersion: 1.0.11\nLibs: -L\$${libdir} -lXau\nCflags: -I\$${includedir}\n" > $(SYSROOT_DIR)/usr/lib/pkgconfig/xau.pc
	@printf "prefix=/usr\nexec_prefix=\$${prefix}\nlibdir=\$${exec_prefix}/lib\nincludedir=\$${prefix}/include\n\nName: Xdmcp\nDescription: X Display Manager Control Protocol library\nVersion: 1.1.5\nLibs: -L\$${libdir} -lXdmcp\nCflags: -I\$${includedir}\n" > $(SYSROOT_DIR)/usr/lib/pkgconfig/xdmcp.pc
	@printf "prefix=/usr\nexec_prefix=\$${prefix}\nlibdir=\$${exec_prefix}/lib\nincludedir=\$${prefix}/include\n\nName: zlib\nDescription: zlib compression library\nVersion: 1.3.1\nLibs: -L\$${libdir} -lz\nCflags: -I\$${includedir}\n" > $(SYSROOT_DIR)/usr/lib/pkgconfig/zlib.pc
	@printf "prefix=/usr\nexec_prefix=\$${prefix}\nlibdir=\$${exec_prefix}/lib\nincludedir=\$${prefix}/include\n\nName: libxcvt\nDescription: VESA CVT standard timing modelines generator\nVersion: 0.1.2\nLibs: -L\$${libdir} -lxcvt\nCflags: -I\$${includedir}\n" > $(SYSROOT_DIR)/usr/lib/pkgconfig/libxcvt.pc
	@printf "prefix=/usr\nexec_prefix=\$${prefix}\nlibdir=\$${exec_prefix}/lib\nincludedir=\$${prefix}/include\n\nName: pciaccess\nDescription: Generic PCI access library\nVersion: 0.17\nLibs: -L\$${libdir} -lpciaccess\nCflags: -I\$${includedir}\n" > $(SYSROOT_DIR)/usr/lib/pkgconfig/pciaccess.pc
	@printf "prefix=/usr\nexec_prefix=\$${prefix}\nlibdir=\$${exec_prefix}/lib\nincludedir=\$${prefix}/include\n\nName: pixman-1\nDescription: The pixman library (version 1)\nVersion: 0.43.4\nLibs: -L\$${libdir} -lpixman-1\nCflags: -I\$${includedir}/pixman-1 -I\$${includedir}\n" > $(SYSROOT_DIR)/usr/lib/pkgconfig/pixman-1.pc
	@printf "prefix=/usr\nexec_prefix=\$${prefix}\nlibdir=\$${exec_prefix}/lib\nincludedir=\$${prefix}/include\n\nName: fontenc\nDescription: The fontenc Library\nVersion: 1.1.4\nLibs: -L\$${libdir} -lfontenc\nCflags: -I\$${includedir}\n" > $(SYSROOT_DIR)/usr/lib/pkgconfig/fontenc.pc
	@cp -f $(SYSROOT_DIR)/usr/lib/pkgconfig/*.pc $(SYSROOT_DIR)/usr/share/pkgconfig/ 2>/dev/null || true
	@rm -f $(SYSROOT_DIR)/usr/lib/*.la
	@touch $@

LIBC_DEPS := $(LIBC_C_OBJS:.o=.d)
-include $(LIBC_DEPS)

sysroot: $(SYSROOT_STAMP)
