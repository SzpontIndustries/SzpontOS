# SzpontOS Global Configuration and Toolchain Setup
# Included by root Makefile and subsystem Makefiles

.DEFAULT_GOAL := all

ifndef CONFIG_MK
CONFIG_MK := 1

# Detect Root Directory if not set
ROOT_DIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

OS_NAME := szpontos
ARCH    := x86_64

# ==============================================================================
# Parallel Build Configuration
# ==============================================================================
NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
JOBS  ?= $(NPROC)
export MAKEFLAGS += -j$(JOBS)

# ==============================================================================
# Toolchain Auto-detection (Prefer GCC, fallback to Clang)
# ==============================================================================

# C Compiler detection
ifneq ($(shell which x86_64-elf-gcc 2>/dev/null),)
    CC := x86_64-elf-gcc
    TOOLCHAIN_TYPE := gcc
else ifneq ($(shell which x86_64-linux-gnu-gcc 2>/dev/null),)
    CC := x86_64-linux-gnu-gcc
    TOOLCHAIN_TYPE := gcc
else ifeq ($(shell uname -s),Linux)
    CC := gcc
    TOOLCHAIN_TYPE := gcc
else ifneq ($(shell which /opt/homebrew/opt/llvm/bin/clang 2>/dev/null),)
    CC := /opt/homebrew/opt/llvm/bin/clang
    TOOLCHAIN_TYPE := clang
else
    CC := clang
    TOOLCHAIN_TYPE := clang
endif

# Linker detection
ifneq ($(shell which x86_64-elf-ld 2>/dev/null),)
    LD := x86_64-elf-ld
else ifneq ($(shell which x86_64-linux-gnu-ld 2>/dev/null),)
    LD := x86_64-linux-gnu-ld
else ifneq ($(shell which ld.lld 2>/dev/null),)
    LD := ld.lld
else ifneq ($(shell which /opt/homebrew/opt/lld/bin/ld.lld 2>/dev/null),)
    LD := /opt/homebrew/opt/lld/bin/ld.lld
else
    LD := ld
endif

# Archiver detection
ifneq ($(shell which x86_64-elf-ar 2>/dev/null),)
    AR := x86_64-elf-ar
else ifneq ($(shell which x86_64-linux-gnu-ar 2>/dev/null),)
    AR := x86_64-linux-gnu-ar
else
    AR := ar
endif

# Ranlib detection
ifneq ($(shell which x86_64-elf-ranlib 2>/dev/null),)
    RANLIB := x86_64-elf-ranlib
else ifneq ($(shell which x86_64-linux-gnu-ranlib 2>/dev/null),)
    RANLIB := x86_64-linux-gnu-ranlib
else
    RANLIB := ranlib
endif

NASM    := nasm
XORRISO := xorriso
QEMU    := qemu-system-x86_64

# ==============================================================================
# Global Directories
# ==============================================================================
BUILD_DIR           := $(ROOT_DIR)/build
ISO_DIR             := $(BUILD_DIR)/iso_root
ROOTFS_DIR          := $(BUILD_DIR)/rootfs
SYSROOT_DIR         := $(BUILD_DIR)/sysroot
SYSROOT_STAMP       := $(SYSROOT_DIR)/.sysroot_installed
ROOTFS_SKELETON_DIR := $(ROOT_DIR)/userland/skeleton
MODULE_DIR          := $(ROOTFS_DIR)/lib/modules

$(BUILD_DIR) $(ROOTFS_DIR) $(SYSROOT_DIR) $(ISO_DIR) $(MODULE_DIR):
	@mkdir -p $@

# ==============================================================================
# Output Binaries & Images
# ==============================================================================
KERNEL_ELF := $(BUILD_DIR)/$(OS_NAME)-kernel
ISO_IMAGE  := $(BUILD_DIR)/$(OS_NAME).iso
DISK_IMAGE := $(BUILD_DIR)/disk.img

# Core Libraries & Objects
CRT0_O     := $(BUILD_DIR)/libc/crt0.o
LIBC_A     := $(BUILD_DIR)/libc/libc.a
LIBC_SO    := $(ROOTFS_DIR)/lib/libc.so
LIBM_A     := $(BUILD_DIR)/libc/libm.a
LIBM_SO    := $(ROOTFS_DIR)/lib/libm.so
LIBDL_A    := $(BUILD_DIR)/libc/libdl.a
LIBCALC_SO := $(ROOTFS_DIR)/lib/libcalc.so

# Third-party Ports Targets
NCURSES_BUILD_DIR   := $(BUILD_DIR)/third_party/ncurses
LIBNCURSES_A        := $(SYSROOT_DIR)/usr/lib/libncurses.a
NANO_BUILD_DIR      := $(BUILD_DIR)/third_party/nano
FILE_BUILD_DIR      := $(BUILD_DIR)/third_party/file
MAGIC_DB            := $(BUILD_DIR)/rootfs/etc/magic
ZSH_BUILD_DIR       := $(BUILD_DIR)/third_party/zsh
ZLIB_BUILD_DIR      := $(BUILD_DIR)/third_party/zlib
LIBZ_A              := $(SYSROOT_DIR)/usr/lib/libz.a
GIT_BUILD_DIR       := $(BUILD_DIR)/third_party/git
FASTFETCH_BUILD_DIR := $(BUILD_DIR)/third_party/fastfetch
OPENSSL_BUILD_DIR   := $(BUILD_DIR)/third_party/openssl
CURL_BUILD_DIR      := $(BUILD_DIR)/third_party/curl
OPENSSH_BUILD_DIR   := $(BUILD_DIR)/third_party/openssh
OPENSSH_SRC_DIR     := $(abspath third_party/openssh)

# Dynamic Kernel Modules
MODULES := \
    $(MODULE_DIR)/hello.sko \
    $(MODULE_DIR)/dummy_dev.sko

# Userland Programs List
USER_PROGS := \
    $(ROOTFS_DIR)/bin/init \
    $(ROOTFS_DIR)/bin/sh \
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
    $(ROOTFS_DIR)/bin/file \
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
    $(ROOTFS_DIR)/bin/SzpontX11 \
    $(ROOTFS_DIR)/bin/Xorg \
    $(ROOTFS_DIR)/bin/startx \
    $(ROOTFS_DIR)/bin/szpontdesktop \
    $(ROOTFS_DIR)/bin/szponterm \
    $(ROOTFS_DIR)/bin/xterm \
    $(ROOTFS_DIR)/bin/nano \
    $(ROOTFS_DIR)/bin/zsh \
    $(ROOTFS_DIR)/bin/fastfetch \
    $(ROOTFS_DIR)/bin/git \
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
    $(ROOTFS_DIR)/bin/ptytest \
    $(ROOTFS_DIR)/bin/kqueuetest \
    $(ROOTFS_DIR)/bin/grep \
    $(ROOTFS_DIR)/bin/find \
    $(ROOTFS_DIR)/bin/top \
    $(ROOTFS_DIR)/bin/reboot \
    $(ROOTFS_DIR)/bin/shutdown \
    $(ROOTFS_DIR)/bin/poweroff \
    $(ROOTFS_DIR)/bin/ls \
    $(ROOTFS_DIR)/bin/lspci \
    $(ROOTFS_DIR)/bin/lsusb \
    $(ROOTFS_DIR)/bin/donut \
    $(ROOTFS_DIR)/bin/mousetest \
    $(ROOTFS_DIR)/bin/unixtest \
    $(ROOTFS_DIR)/bin/gittest \
    $(ROOTFS_DIR)/bin/openssl \
    $(ROOTFS_DIR)/bin/curl


# ==============================================================================
# Compilation Flags
# ==============================================================================

# Kernel Compilation Flags
CFLAGS := \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-lto \
    -fno-pie \
    -fno-pic \
    -mno-80387 \
    -mno-mmx \
    -mno-sse \
    -mno-sse2 \
    -mno-red-zone \
    -mcmodel=kernel \
    -Wall \
    -Wextra \
    -std=c17 \
    -O2 \
    -g \
    -MMD \
    -MP \
    -I $(ROOT_DIR)/kernel/include \
    -I $(BUILD_DIR)/include \
    -I $(ROOT_DIR)

ifeq ($(TOOLCHAIN_TYPE),clang)
    CFLAGS += -target $(ARCH)-unknown-none-elf
endif

ASMFLAGS := -f elf64

LDFLAGS := \
    -nostdlib \
    -static \
    -m elf_x86_64 \
    -z max-page-size=0x1000 \
    -T $(ROOT_DIR)/kernel/linker.ld

# Userland Compilation Flags
USER_CFLAGS := \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-lto \
    -fPIC \
    -mno-red-zone \
    -Wall \
    -Wextra \
    -std=c17 \
    -O2 \
    -g \
    -MMD \
    -MP \
    -I $(ROOT_DIR)/libc/include

ifeq ($(TOOLCHAIN_TYPE),clang)
    USER_CFLAGS += -target $(ARCH)-unknown-none-elf
endif

USER_LDFLAGS := \
    -nostdlib \
    -m elf_x86_64 \
    -z max-page-size=0x1000

MODULE_CFLAGS := $(CFLAGS) -mcmodel=kernel -fno-pic -fno-pie

endif
