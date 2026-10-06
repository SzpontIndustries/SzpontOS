# SzpontOS Bootable ISO & Initramfs Image Packaging Rules
# Included by root Makefile

.PHONY: limine initramfs disk iso

# ==============================================================================
# Download and prepare Limine bootloader
# ==============================================================================
limine-bin/limine-bios.sys:
	@if [ ! -d limine-bin ]; then \
		echo "  [GIT] Pobieranie Limine bootloader v8.x-binary..."; \
		git clone https://github.com/limine-bootloader/limine.git --branch=v8.x-binary --depth=1 limine-bin; \
	fi
	@if [ -d limine-bin ] && [ ! -f limine-bin/limine-bios.sys ]; then \
		$(MAKE) -C limine-bin; \
	fi

limine-bin/limine: limine-bin/limine-bios.sys $(wildcard limine-bin/limine.c)
	@$(MAKE) -C limine-bin limine CC=cc

limine: limine-bin/limine-bios.sys limine-bin/limine

# ==============================================================================
# Build initramfs archive
# NOTE (single-shot fix): initramfs is the serialization point. It waits for
# the full `build` (sysroot -> third-party -> userland/modules -> kernel).
# USERLAND_STAMP already chains after THIRDPARTY_STAMP (see root Makefile), so
# listing stamps here is defense-in-depth for direct `make initramfs` calls.
# verify_rootfs.py MUST fail loudly (no `|| true`) - incomplete rootfs from
# a first pass must stop the build instead of producing a broken ISO that
# only succeeds on the 2nd `make`.
# ==============================================================================
SKELETON_FILES := $(shell find $(ROOTFS_SKELETON_DIR) -type f 2>/dev/null)
$(BUILD_DIR)/initramfs.tar: build $(USERLAND_STAMP) $(MODULES_STAMP) $(THIRDPARTY_STAMP) $(KERNEL_ELF) $(SKELETON_FILES) | $(ROOTFS_DIR)
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(ROOTFS_DIR)/bin $(ROOTFS_DIR)/sbin $(ROOTFS_DIR)/lib \
		$(ROOTFS_DIR)/usr/bin $(ROOTFS_DIR)/usr/sbin $(ROOTFS_DIR)/usr/lib $(ROOTFS_DIR)/usr/tbin \
		$(ROOTFS_DIR)/dev $(ROOTFS_DIR)/etc $(ROOTFS_DIR)/etc/X11/app-defaults \
		$(ROOTFS_DIR)/proc $(ROOTFS_DIR)/sys $(ROOTFS_DIR)/mnt \
		$(ROOTFS_DIR)/tmp $(ROOTFS_DIR)/tmp/.X11-unix \
		$(ROOTFS_DIR)/var $(ROOTFS_DIR)/var/log $(ROOTFS_DIR)/var/run $(ROOTFS_DIR)/var/empty $(ROOTFS_DIR)/var/lib/xkb \
		$(ROOTFS_DIR)/root $(ROOTFS_DIR)/home $(ROOTFS_DIR)/home/szpont \
		$(ROOTFS_DIR)/usr/share $(ROOTFS_DIR)/usr/share/artwork $(ROOTFS_DIR)/usr/share/applications $(ROOTFS_DIR)/usr/share/X11/app-defaults $(ROOTFS_DIR)/usr/lib/X11/app-defaults
	@chmod 1777 $(ROOTFS_DIR)/tmp $(ROOTFS_DIR)/tmp/.X11-unix 2>/dev/null || true
	@chmod 755 $(ROOTFS_DIR)/var/empty 2>/dev/null || true
	@if [ -d $(ROOTFS_SKELETON_DIR) ]; then cp -a $(ROOTFS_SKELETON_DIR)/. $(ROOTFS_DIR)/ 2>/dev/null || true; fi
	@if [ -f $(ROOTFS_SKELETON_DIR)/etc/X11/app-defaults/XTerm ]; then \
		cp -f $(ROOTFS_SKELETON_DIR)/etc/X11/app-defaults/XTerm $(ROOTFS_DIR)/usr/share/X11/app-defaults/XTerm; \
		cp -f $(ROOTFS_SKELETON_DIR)/etc/X11/app-defaults/XTerm $(ROOTFS_DIR)/usr/share/X11/app-defaults/XTerm-color; \
		cp -f $(ROOTFS_SKELETON_DIR)/etc/X11/app-defaults/XTerm $(ROOTFS_DIR)/usr/lib/X11/app-defaults/XTerm; \
		cp -f $(ROOTFS_SKELETON_DIR)/etc/X11/app-defaults/XTerm $(ROOTFS_DIR)/usr/lib/X11/app-defaults/XTerm-color; \
	fi
	@if [ -f $(ROOTFS_SKELETON_DIR)/etc/X11/app-defaults/SzponTerm ]; then \
		cp -f $(ROOTFS_SKELETON_DIR)/etc/X11/app-defaults/SzponTerm $(ROOTFS_DIR)/usr/share/X11/app-defaults/SzponTerm; \
		cp -f $(ROOTFS_SKELETON_DIR)/etc/X11/app-defaults/SzponTerm $(ROOTFS_DIR)/usr/share/X11/app-defaults/SzponTerm-color; \
		cp -f $(ROOTFS_SKELETON_DIR)/etc/X11/app-defaults/SzponTerm $(ROOTFS_DIR)/usr/lib/X11/app-defaults/SzponTerm; \
		cp -f $(ROOTFS_SKELETON_DIR)/etc/X11/app-defaults/SzponTerm $(ROOTFS_DIR)/usr/lib/X11/app-defaults/SzponTerm-color; \
	fi
	@if [ -f $(BUILD_DIR)/third_party/libX11/nls/C/XLC_LOCALE ]; then \
		mkdir -p $(ROOTFS_DIR)/usr/share/X11/locale/C; \
		cp -f $(BUILD_DIR)/third_party/libX11/nls/C/XLC_LOCALE $(ROOTFS_DIR)/usr/share/X11/locale/C/; \
		cp -f $(BUILD_DIR)/third_party/libX11/nls/locale.dir $(ROOTFS_DIR)/usr/share/X11/locale/; \
		cp -f $(BUILD_DIR)/third_party/libX11/nls/locale.alias $(ROOTFS_DIR)/usr/share/X11/locale/; \
	fi
	@mkdir -p $(ROOTFS_DIR)/usr/share/artwork
	@if [ -d artwork ]; then cp -r artwork/* $(ROOTFS_DIR)/usr/share/artwork/ 2>/dev/null || true; fi
	@if [ -f artwork/szpont-detected.jpg ]; then cp artwork/szpont-detected.jpg $(ROOTFS_DIR)/usr/share/artwork/szpont-detected.png 2>/dev/null || true; fi
	@rm -f $(ROOTFS_DIR)/lib/*.a $(ROOTFS_DIR)/usr/lib/*.a
	@./scripts/verify_rootfs.py $(ROOTFS_DIR)
	@./scripts/make_initramfs.py $(ROOTFS_DIR) $(BUILD_DIR)/initramfs.tar

initramfs: $(BUILD_DIR)/initramfs.tar

# ==============================================================================
# Generate ext2 Disk Image
# ==============================================================================
$(DISK_IMAGE): scripts/make_ext2_disk.py
	@mkdir -p $(BUILD_DIR)
	@echo "  [EXT2] Generowanie obrazu dysku ext2 $(DISK_IMAGE)..."
	@python3 scripts/make_ext2_disk.py $(DISK_IMAGE)

disk: $(DISK_IMAGE)

# ==============================================================================
# Build bootable ISO
# NOTE (single-shot fix): ISO strictly depends on `build` first, so even
# `make -j` cannot run xorriso/limine packaging concurrently with rootfs
# population. `iso` is a convenience alias only.
# ==============================================================================
$(ISO_IMAGE): build $(KERNEL_ELF) $(BUILD_DIR)/initramfs.tar limine-bin/limine-bios.sys limine-bin/limine $(DISK_IMAGE)
	@echo "  [ISO] Tworzenie obrazu rozruchowego $(ISO_IMAGE)..."
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot $(ISO_DIR)/boot/limine $(ISO_DIR)/EFI/BOOT
	@cp $(KERNEL_ELF) $(ISO_DIR)/boot/$(OS_NAME)-kernel
	@cp $(BUILD_DIR)/initramfs.tar $(ISO_DIR)/boot/initramfs.tar
	@cp limine.conf $(ISO_DIR)/boot/limine/limine.conf
	@cp limine-bin/limine-bios.sys $(ISO_DIR)/boot/limine/
	@cp limine-bin/limine-bios-cd.bin $(ISO_DIR)/boot/limine/
	@cp limine-bin/limine-uefi-cd.bin $(ISO_DIR)/boot/limine/
	@cp limine-bin/BOOTX64.EFI $(ISO_DIR)/EFI/BOOT/
	@cp limine-bin/BOOTIA32.EFI $(ISO_DIR)/EFI/BOOT/ 2>/dev/null || true
	@$(XORRISO) -as mkisofs -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_DIR) -o $(ISO_IMAGE) >/dev/null 2>&1
	@./limine-bin/limine bios-install $(ISO_IMAGE)
	@echo "  [OK]  Obraz ISO gotowy: $(ISO_IMAGE)"

iso: $(ISO_IMAGE)
