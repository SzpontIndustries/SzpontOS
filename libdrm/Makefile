# SzpontOS Native libdrm Makefile
# (C) Copyright by Szpont Industries. All rights reserved.

ROOT_DIR ?= $(abspath ..)
include $(ROOT_DIR)/config.mk

LIBDRM_SRCS := \
    $(ROOT_DIR)/libdrm/src/xf86drm.c \
    $(ROOT_DIR)/libdrm/src/xf86drmMode.c \
    $(ROOT_DIR)/libdrm/src/syncobj.c

LIBDRM_OBJS := $(patsubst $(ROOT_DIR)/libdrm/%.c, $(BUILD_DIR)/libdrm/%.o, $(LIBDRM_SRCS))

LIBDRM_SO := $(BUILD_DIR)/libdrm/libdrm.so
LIBDRM_PC := $(SYSROOT_DIR)/usr/lib/pkgconfig/libdrm.pc

LIBDRM_CFLAGS := $(USER_CFLAGS) -fPIC \
    -isystem $(abspath $(SYSROOT_DIR))/usr/include \
    -I$(ROOT_DIR)/libdrm/include

.PHONY: all libdrm install clean

all: libdrm install

libdrm: $(LIBDRM_SO)

$(BUILD_DIR)/libdrm/%.o: $(ROOT_DIR)/libdrm/%.c | $(SYSROOT_STAMP)
	@mkdir -p $(dir $@)
	@echo "  [CC-LIBDRM] $<"
	@$(CC) $(LIBDRM_CFLAGS) -c $< -o $@

$(LIBDRM_SO): $(LIBDRM_OBJS) | $(ROOTFS_DIR) $(SYSROOT_STAMP)
	@mkdir -p $(BUILD_DIR)/libdrm $(ROOTFS_DIR)/lib $(SYSROOT_DIR)/usr/lib
	@echo "  [LD-LIBDRM] $@"
	@$(LD) -shared -soname libdrm.so.2 $(LIBDRM_OBJS) -L$(abspath $(SYSROOT_DIR))/usr/lib -lc -o $@

install: $(LIBDRM_SO)
	@mkdir -p $(ROOTFS_DIR)/lib $(SYSROOT_DIR)/usr/lib $(SYSROOT_DIR)/usr/lib/pkgconfig \
	          $(SYSROOT_DIR)/usr/include/libdrm $(SYSROOT_DIR)/usr/include/drm
	@cp -f $(LIBDRM_SO) $(ROOTFS_DIR)/lib/libdrm.so
	@ln -sf libdrm.so $(ROOTFS_DIR)/lib/libdrm.so.2
	@cp -f $(LIBDRM_SO) $(SYSROOT_DIR)/usr/lib/libdrm.so
	@ln -sf libdrm.so $(SYSROOT_DIR)/usr/lib/libdrm.so.2
	@cp -f $(ROOT_DIR)/libdrm/include/drm/*.h $(SYSROOT_DIR)/usr/include/drm/
	@cp -f $(ROOT_DIR)/libdrm/include/drm/*.h $(SYSROOT_DIR)/usr/include/libdrm/
	@cp -f $(ROOT_DIR)/libdrm/include/xf86drm*.h $(SYSROOT_DIR)/usr/include/libdrm/
	@cp -f $(ROOT_DIR)/libdrm/include/xf86drm*.h $(SYSROOT_DIR)/usr/include/
	@sed -e 's|@prefix@|/usr|g' \
	     -e 's|@exec_prefix@|/usr|g' \
	     -e 's|@libdir@|/usr/lib|g' \
	     -e 's|@includedir@|/usr/include|g' \
	     $(ROOT_DIR)/libdrm/libdrm.pc.in > $(LIBDRM_PC)
	@echo "  [OK]  libdrm installed to sysroot and rootfs"

clean:
	@rm -rf $(BUILD_DIR)/libdrm
