ROOT_DIR := $(abspath ../..)
include $(ROOT_DIR)/config.mk
.PHONY: all
all:
	@$(MAKE) -C $(ROOT_DIR)/userland/bin $(ROOTFS_DIR)/usr/tbin/corecheck
