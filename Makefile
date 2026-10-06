# SzpontOS Top-Level Orchestration Makefile
# Multi-platform build system supporting macOS & Linux with GCC / Clang

ROOT_DIR := $(abspath .)
include $(ROOT_DIR)/config.mk
include $(ROOT_DIR)/mk/third_party.mk
include $(ROOT_DIR)/mk/iso.mk
include $(ROOT_DIR)/mk/qemu.mk

# ==============================================================================
# Phony Targets
# ==============================================================================
.PHONY: all build toolchain-info kernel libc libdrm userland modules sysroot \
        third-party clean distclean compile_commands.json compile-commands bear

# Default target: build bootable ISO image.
# NOTE (single-shot fix): ISO strictly depends on `build` so even `make -j`
# cannot run ISO packaging concurrently with rootfs population. Previously
# `all: build $(ISO_IMAGE)` raced initramfs/xorriso against userland/third-party.
all: $(ISO_IMAGE)

# Display detected toolchain information
toolchain-info:
	@echo "  [TOOLCHAIN] CC:    $(CC) ($(TOOLCHAIN_TYPE))"
	@echo "  [TOOLCHAIN] LD:    $(LD)"
	@echo "  [TOOLCHAIN] AR:    $(AR)"
	@echo "  [TOOLCHAIN] ASM:   $(NASM)"
	@echo "  [TOOLCHAIN] ISO:   $(XORRISO)"
	@echo "  [TOOLCHAIN] CORES: $(NPROC) (Parallel Jobs: $(JOBS))"
	@echo "  [TOOLCHAIN] MODE:  top-level serial orchestration, leaves -j$(JOBS)"

# Build all core components - SERIAL PHASES (single-shot fix):
#   phase1: sysroot (libc headers/libs)
#   phase2: third-party libs (needs sysroot)
#   phase3: userland + modules (need third-party .so set)
#   kernel builds independently in parallel (no sysroot dependency) but
#   `build` still waits for it. Order is enforced via inter-stamp deps below,
#   not via flat prerequisite list.
build: toolchain-info $(SYSROOT_STAMP) $(THIRDPARTY_STAMP) $(USERLAND_STAMP) $(MODULES_STAMP) $(KERNEL_ELF)

KERNEL_SRCS := $(shell find $(ROOT_DIR)/kernel/src $(ROOT_DIR)/kernel/include $(ROOT_DIR)/kernel/arch -type f 2>/dev/null)
LIBC_SRCS   := $(shell find $(ROOT_DIR)/libc/src $(ROOT_DIR)/libc/include -type f 2>/dev/null)

# Subsystem delegates
# NOTE: kernel is independent of sysroot (freestanding, -nostdlib) so it may
# build in parallel with sysroot/third-party. All other phases are chained.
$(KERNEL_ELF): $(KERNEL_SRCS) $(ROOT_DIR)/kernel/linker.ld $(ROOT_DIR)/kernel/Makefile
	@$(MAKE) -j$(JOBS) -C $(ROOT_DIR)/kernel

kernel: $(KERNEL_ELF)

# Single-shot fix: LIBC_SO is produced as part of sysroot population
# (libc/Makefile builds LIBC_SO then copies to sysroot). Previously two
# concurrent recipes (`libc` + `sysroot`) raced on the same
# BUILD/libc/*.o files. Now LIBC_SO is just an alias ordered after sysroot.
$(LIBC_SO): $(SYSROOT_STAMP)
	@test -f $@ || { echo "  [ERR] $@ missing after sysroot build"; exit 1; }

libc: $(SYSROOT_STAMP)

$(SYSROOT_STAMP): $(LIBC_SRCS) $(SYSROOT_TP_SRCS) $(ROOT_DIR)/libc/Makefile $(ROOT_DIR)/config.mk | submodules-check
	@$(MAKE) -j$(JOBS) -C $(ROOT_DIR)/libc sysroot

sysroot: $(SYSROOT_STAMP)

libdrm: $(ROOTFS_DIR)/usr/lib/libdrm.so

libgbm: $(ROOTFS_DIR)/usr/lib/libgbm.so

MODULES_SRCS := $(shell find $(ROOT_DIR)/modules -type f 2>/dev/null)
# Single owner of MODULES_STAMP is top-level (modules/Makefile must NOT touch
# it - see modules/Makefile). Normal dep on SYSROOT (not order-only) so
# sysroot header changes correctly trigger module rebuilds.
$(MODULES_STAMP): $(MODULES_SRCS) $(SYSROOT_STAMP) $(ROOT_DIR)/modules/Makefile $(ROOT_DIR)/config.mk
	@$(MAKE) -j$(JOBS) -C $(ROOT_DIR)/modules
	@mkdir -p $(dir $@) && touch $@

modules: $(MODULES_STAMP)

USERLAND_SRCS := $(shell find $(ROOT_DIR)/userland -type f 2>/dev/null)
# Single owner of USERLAND_STAMP is top-level (userland/Makefile must NOT touch
# it). Chained AFTER third-party so X11/mesa/openssl headers + .so files exist
# before any userland compile starts. This was the main "repeat make" cause.
$(USERLAND_STAMP): $(USERLAND_SRCS) $(THIRDPARTY_STAMP) $(ROOT_DIR)/userland/Makefile $(ROOT_DIR)/config.mk
	@$(MAKE) -j$(JOBS) -C $(ROOT_DIR)/userland
	@mkdir -p $(dir $@) && touch $@

userland: $(USERLAND_STAMP)

third-party: $(THIRDPARTY_STAMP)

# ==============================================================================
# Development Tooling & Compilation Database (clangd / IDE IntelliSense)
# NOTE (single-shot fix): the DB is generated STATICALLY by
# scripts/generate_compile_commands.py (flag sets mirror config.mk + component
# makefiles, incl. C++). The old `bear -- $(MAKE) clean all` approach is gone:
# it forced a full rebuild, captured host-tool noise (makestrs, autotools
# probes, meson try-compiles) and ccache-wrapped compiler paths, and deleted
# the DB first - so any failed run left the linter with nothing.
# ==============================================================================
compile_commands.json compile-commands bear:
	@echo "  [CLANGD-DB] Generowanie compile_commands.json (statycznie, bez przebudowy)..."
	@python3 $(ROOT_DIR)/scripts/generate_compile_commands.py
	@python3 -c "import json,sys; db=json.load(open('compile_commands.json')); assert isinstance(db,list) and db, 'pusta baza'; print('  [OK]   compile_commands.json: %d wpisow, JSON poprawny.' % len(db))"

# ==============================================================================
# Cleanup Rules
# ==============================================================================
clean:
	@echo "  [CLEAN] Czyszczenie katalogu build..."
	@rm -rf $(BUILD_DIR)

distclean: clean
	@echo "  [CLEAN] Usuwanie pobranych binariów Limine i bazy kompilacji..."
	@rm -rf limine-bin compile_commands.json
