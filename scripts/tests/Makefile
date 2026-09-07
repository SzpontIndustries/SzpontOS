ROOT_DIR := $(abspath ../..)
include $(ROOT_DIR)/config.mk
all: $(ROOTFS_DIR)/bin/corecheck
$(ROOTFS_DIR)/bin/corecheck: corecheck.c $(LIBC_SO) $(CRT0_O)
	$(CC) $(USER_CFLAGS) -fno-builtin -c $< -o $(BUILD_DIR)/corecheck.o
	$(CC) -nostdlib -B$(SYSROOT_DIR)/usr/lib $(CRT0_O) $(BUILD_DIR)/corecheck.o -L$(ROOTFS_DIR)/lib -lc -o $@
