# SzpontOS Userland Master Makefile
ROOT_DIR ?= $(abspath ..)
include $(ROOT_DIR)/config.mk

.PHONY: all userland skeleton init sh lib bin xserver

all: userland

userland: skeleton init sh lib bin xserver

skeleton:
	@mkdir -p $(ROOTFS_DIR)/bin $(ROOTFS_DIR)/lib $(ROOTFS_DIR)/dev $(ROOTFS_DIR)/etc $(ROOTFS_DIR)/proc $(ROOTFS_DIR)/mnt
	@if [ -d $(ROOTFS_SKELETON_DIR) ]; then cp -r $(ROOTFS_SKELETON_DIR)/* $(ROOTFS_DIR)/ 2>/dev/null || true; fi

init: skeleton
	@$(MAKE) -j$(JOBS) -C $(ROOT_DIR)/userland/init

sh: skeleton
	@$(MAKE) -j$(JOBS) -C $(ROOT_DIR)/userland/sh

lib: skeleton
	@$(MAKE) -j$(JOBS) -C $(ROOT_DIR)/userland/lib

bin: skeleton
	@$(MAKE) -j$(JOBS) -C $(ROOT_DIR)/userland/bin

xserver: skeleton
	@$(MAKE) -j$(JOBS) -C $(ROOT_DIR)/userland/xserver
