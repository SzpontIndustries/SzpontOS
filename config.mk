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
# NOTE (single-shot fix): top-level orchestration is intentionally SERIAL.
# Parallelism lives ONLY inside subsystem builds (explicit -j$(JOBS) in
# sub-make/ninja recipes). Auto-injecting -j into MAKEFLAGS caused races:
# sysroot headers vs third-party configure vs userland compile vs ISO
# packaging running concurrently, requiring repeated `make` invocations.
# Do NOT re-add `MAKEFLAGS += -j` here. Use `make -j<N>` explicitly only
# for debugging; supported path is serial orchestration + parallel leaves.
# ==============================================================================
NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
JOBS  ?= $(NPROC)

# Synchronized output so parallel leaf builds stay readable (GNU make >= 4.0
# ONLY - macOS ships make 3.81 which aborts on unknown --output-sync).
# Version guard: MAKE_VERSION is e.g. "3.81" or "4.4.1".
MAKE_GE_4 := $(shell expr "$(MAKE_VERSION)" ">=" "4.0" 2>/dev/null || echo 0)
ifeq ($(MAKE_GE_4),1)
ifeq ($(filter --output-sync%,$(MAKEFLAGS)),)
MAKEFLAGS += --output-sync=recurse
endif
endif

# Portable in-place sed (works on both GNU and BSD/macOS sed).
# Usage: $(SED_INPLACE) 's/foo/bar/' <file>
# Implemented via backup suffix + immediate cleanup (atomic on both seds).
SED_INPLACE := sed -i.bak
SED_INPLACE_CLEAN := find . -name '*.bak' -delete 2>/dev/null; true
# Helper: portable `sed -i` replacement without backup litter.
# Call as: $(call sed_inplace,EXPR,FILE)
define sed_inplace
	sed -i.bak $(1) $(2) && rm -f $(2).bak
endef

# Directory for configure/meson serialization locks (mkdir-based, portable
# across macOS/Linux, no `flock` dependency).
BUILD_LOCK_DIR := $(ROOT_DIR)/build/.locks

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

# C++ Compiler detection
ifneq ($(shell which x86_64-elf-g++ 2>/dev/null),)
    CXX := x86_64-elf-g++
else ifneq ($(shell which x86_64-linux-gnu-g++ 2>/dev/null),)
    CXX := x86_64-linux-gnu-g++
else ifeq ($(shell uname -s),Linux)
    CXX := g++
else ifneq ($(shell which /opt/homebrew/opt/llvm/bin/clang++ 2>/dev/null),)
    CXX := /opt/homebrew/opt/llvm/bin/clang++
else
    CXX := clang++
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

# Compiler Caching Support (ccache)
CC_RAW  := $(CC)
CXX_RAW := $(CXX)
CCACHE  ?= $(shell which ccache 2>/dev/null)
ifneq ($(CCACHE),)
    CC  := $(CCACHE) $(CC_RAW)
    CXX := $(CCACHE) $(CXX_RAW)
endif

# Autotools M4 Include Flags (portable macOS / Linux)
ACLOCAL_EXTRA_DIRS := $(shell for d in /opt/homebrew/share/aclocal /usr/share/aclocal /usr/local/share/aclocal; do test -d $$d && echo "-I $$d"; done)
ACLOCAL_FLAGS      := -I $(abspath $(ROOT_DIR)/third_party/util-macros) -I $(abspath $(ROOT_DIR)/third_party/xorgproto) $(ACLOCAL_EXTRA_DIRS)

NASM    := nasm
XORRISO := xorriso
QEMU    := qemu-system-x86_64

# ==============================================================================
# Global Directories & Subsystem Caching Stamps
# ==============================================================================
BUILD_DIR           := $(ROOT_DIR)/build
ISO_DIR             := $(BUILD_DIR)/iso_root
ROOTFS_DIR          := $(BUILD_DIR)/rootfs
SYSROOT_DIR         := $(BUILD_DIR)/sysroot
SYSROOT_STAMP       := $(SYSROOT_DIR)/.sysroot_installed
USERLAND_STAMP      := $(BUILD_DIR)/userland/.built
THIRDPARTY_STAMP    := $(BUILD_DIR)/third_party/.built
MODULES_STAMP       := $(BUILD_DIR)/modules/.built
ROOTFS_SKELETON_DIR := $(ROOT_DIR)/userland/skeleton
MODULE_DIR          := $(ROOTFS_DIR)/lib/modules

$(BUILD_DIR) $(ROOTFS_DIR) $(SYSROOT_DIR) $(ISO_DIR) $(MODULE_DIR) $(BUILD_DIR)/userland $(BUILD_DIR)/third_party $(BUILD_DIR)/modules:
	@mkdir -p $@

# ==============================================================================
# Output Binaries & Images
# ==============================================================================
KERNEL_ELF := $(BUILD_DIR)/$(OS_NAME)-kernel
ISO_IMAGE  := $(BUILD_DIR)/$(OS_NAME).iso
DISK_IMAGE := $(BUILD_DIR)/disk.img

# Core Libraries & Objects
CRT0_O     := $(BUILD_DIR)/libc/crt0.o
CRTI_O     := $(BUILD_DIR)/libc/crti.o
CRTN_O     := $(BUILD_DIR)/libc/crtn.o
LIBC_A     := $(BUILD_DIR)/libc/libc.a
LIBC_SO    := $(ROOTFS_DIR)/lib/libc.so
LIBM_A     := $(BUILD_DIR)/libc/libm.a
LIBM_SO    := $(ROOTFS_DIR)/lib/libm.so
LIBDL_A    := $(BUILD_DIR)/libc/libdl.a
LIBSTDCXX_SO := $(ROOTFS_DIR)/usr/lib/libstdc++.so
LIBSTDCXX_A  := $(SYSROOT_DIR)/usr/lib/libstdc++.a

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
XTERM_BUILD_DIR     := $(BUILD_DIR)/third_party/xterm

# Dynamic Kernel Modules
MODULES := \
    $(MODULE_DIR)/hello.sko \
    $(MODULE_DIR)/dummy_dev.sko

# Userland Programs List (/bin - Essential Rescue/Boot Binaries)
USER_BIN_PROGS := \
    $(ROOTFS_DIR)/bin/init \
    $(ROOTFS_DIR)/bin/sh \
    $(ROOTFS_DIR)/bin/cat \
    $(ROOTFS_DIR)/bin/chmod \
    $(ROOTFS_DIR)/bin/chown \
    $(ROOTFS_DIR)/bin/date \
    $(ROOTFS_DIR)/bin/df \
    $(ROOTFS_DIR)/bin/dmesg \
    $(ROOTFS_DIR)/bin/hostname \
    $(ROOTFS_DIR)/bin/kill \
    $(ROOTFS_DIR)/bin/ls \
    $(ROOTFS_DIR)/bin/mkdir \
    $(ROOTFS_DIR)/bin/mount \
    $(ROOTFS_DIR)/bin/ps \
    $(ROOTFS_DIR)/bin/rm \
    $(ROOTFS_DIR)/bin/sleep \
    $(ROOTFS_DIR)/bin/su \
    $(ROOTFS_DIR)/bin/sync \
    $(ROOTFS_DIR)/bin/touch \
    $(ROOTFS_DIR)/bin/uname \
    $(ROOTFS_DIR)/bin/reboot \
    $(ROOTFS_DIR)/bin/shutdown \
    $(ROOTFS_DIR)/bin/poweroff \
    $(ROOTFS_DIR)/bin/ifconfig \
    $(ROOTFS_DIR)/bin/ping \
    $(ROOTFS_DIR)/bin/sysctl \
    $(ROOTFS_DIR)/bin/insmod \
    $(ROOTFS_DIR)/bin/rmmod \
    $(ROOTFS_DIR)/bin/lsmod \
    $(ROOTFS_DIR)/bin/modinfo

# Userland Programs List (/usr/bin - Applications & User Commands)
USER_USR_BIN_PROGS := \
    $(ROOTFS_DIR)/usr/bin/clear \
    $(ROOTFS_DIR)/usr/bin/clock \
    $(ROOTFS_DIR)/usr/bin/env \
    $(ROOTFS_DIR)/usr/bin/free \
    $(ROOTFS_DIR)/usr/bin/uptime \
    $(ROOTFS_DIR)/usr/bin/id \
    $(ROOTFS_DIR)/usr/bin/whoami \
    $(ROOTFS_DIR)/usr/bin/top \
    $(ROOTFS_DIR)/usr/bin/lspci \
    $(ROOTFS_DIR)/usr/bin/lsusb \
    $(ROOTFS_DIR)/usr/bin/sudo \
    $(ROOTFS_DIR)/usr/bin/useradd \
    $(ROOTFS_DIR)/usr/bin/userdel \
    $(ROOTFS_DIR)/usr/bin/groupadd \
    $(ROOTFS_DIR)/usr/bin/killall \
    $(ROOTFS_DIR)/usr/bin/head \
    $(ROOTFS_DIR)/usr/bin/tail \
    $(ROOTFS_DIR)/usr/bin/wc \
    $(ROOTFS_DIR)/usr/bin/grep \
    $(ROOTFS_DIR)/usr/bin/find \
    $(ROOTFS_DIR)/usr/bin/file \
    $(ROOTFS_DIR)/usr/bin/curl \
    $(ROOTFS_DIR)/usr/bin/nc \
    $(ROOTFS_DIR)/usr/bin/httpd \
    $(ROOTFS_DIR)/usr/bin/httpget \
    $(ROOTFS_DIR)/usr/bin/host \
    $(ROOTFS_DIR)/usr/bin/openssl \
    $(ROOTFS_DIR)/usr/bin/git \
    $(ROOTFS_DIR)/usr/bin/nano \
    $(ROOTFS_DIR)/usr/bin/zsh \
    $(ROOTFS_DIR)/usr/bin/fastfetch \
    $(ROOTFS_DIR)/usr/bin/donut \
    $(ROOTFS_DIR)/usr/bin/hello \
    $(ROOTFS_DIR)/usr/bin/startx \
    $(ROOTFS_DIR)/usr/bin/szpontlogin \
    $(ROOTFS_DIR)/usr/bin/szpontdesktop \
    $(ROOTFS_DIR)/usr/bin/szponterm \
    $(ROOTFS_DIR)/usr/bin/xterm \
    $(ROOTFS_DIR)/usr/bin/makaljer \
    $(ROOTFS_DIR)/usr/bin/szpontdetected \
    $(ROOTFS_DIR)/usr/bin/SzpontX11 \
    $(ROOTFS_DIR)/usr/bin/Xorg

# Userland Programs List (/usr/tbin - System & Driver Test Suites)
USER_TEST_PROGS := \
    $(ROOTFS_DIR)/usr/tbin/dltest \
    $(ROOTFS_DIR)/usr/tbin/mathtest \
    $(ROOTFS_DIR)/usr/tbin/threadtest \
    $(ROOTFS_DIR)/usr/tbin/tuitest \
    $(ROOTFS_DIR)/usr/tbin/drmtest \
    $(ROOTFS_DIR)/usr/tbin/randtest \
    $(ROOTFS_DIR)/usr/tbin/tmpfstest \
    $(ROOTFS_DIR)/usr/tbin/ptytest \
    $(ROOTFS_DIR)/usr/tbin/kqueuetest \
    $(ROOTFS_DIR)/usr/tbin/mousetest \
    $(ROOTFS_DIR)/usr/tbin/unixtest \
    $(ROOTFS_DIR)/usr/tbin/gittest \
    $(ROOTFS_DIR)/usr/tbin/cpptest

USER_PROGS := $(USER_BIN_PROGS) $(USER_USR_BIN_PROGS) $(USER_TEST_PROGS)


# ==============================================================================
# Compilation Flags
# ==============================================================================

# Kernel Compilation Flags
CFLAGS := \
    -ffreestanding \
    -D__KERNEL__=1 \
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
    -I $(ROOT_DIR)/kernel/include/uapi \
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
    -D__szpontos__ \
    -D__unix__ \
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
    -I $(ROOT_DIR)/libc/include \
    -I $(ROOT_DIR)/kernel/include/uapi

ifeq ($(TOOLCHAIN_TYPE),clang)
    USER_CFLAGS += -target $(ARCH)-unknown-none-elf
endif

# Userland C++ Compilation Flags
USER_CXXFLAGS := \
    -D__szpontos__ \
    -D__unix__ \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-lto \
    -fPIC \
    -mno-red-zone \
    -Wall \
    -Wextra \
    -std=gnu++17 \
    -O2 \
    -g \
    -MMD \
    -MP \
    -nostdinc++ \
    -isystem $(SYSROOT_DIR)/usr/include/c++ \
    -isystem $(SYSROOT_DIR)/usr/include/c++/bits \
    -isystem $(SYSROOT_DIR)/usr/include/c++/backward \
    -isystem $(SYSROOT_DIR)/usr/include/c++/ext \
    -isystem $(SYSROOT_DIR)/usr/include \
    -I $(SYSROOT_DIR)/usr/include

ifeq ($(TOOLCHAIN_TYPE),clang)
    USER_CXXFLAGS += -target $(ARCH)-unknown-none-elf
endif

USER_LDFLAGS := \
    -nostdlib \
    -m elf_x86_64 \
    -z max-page-size=0x1000

MODULE_CFLAGS := $(CFLAGS) -mcmodel=kernel -fno-pic -fno-pie

endif
