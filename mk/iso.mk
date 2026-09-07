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
# ==============================================================================
SKELETON_FILES := $(shell find $(ROOTFS_SKELETON_DIR) -type f 2>/dev/null)
$(BUILD_DIR)/initramfs.tar: userland modules third-party $(SKELETON_FILES) | $(ROOTFS_DIR)
	@mkdir -p $(BUILD_DIR)
	@if [ -d $(ROOTFS_SKELETON_DIR) ]; then cp -r $(ROOTFS_SKELETON_DIR)/. $(ROOTFS_DIR)/ 2>/dev/null || true; fi
	@mkdir -p $(ROOTFS_DIR)/usr/share/artwork $(ROOTFS_DIR)/usr/share
	@if [ -d artwork ]; then cp -r artwork/* $(ROOTFS_DIR)/usr/share/artwork/ 2>/dev/null || true; fi
	@if [ -d artwork ]; then cp -r artwork/* $(ROOTFS_DIR)/usr/share/ 2>/dev/null || true; fi
	@if [ -f artwork/szpont-detected.jpg ]; then cp artwork/szpont-detected.jpg $(ROOTFS_DIR)/usr/share/artwork/szpont-detected.png 2>/dev/null || true; fi
	@if [ -f artwork/szpont-detected.jpg ]; then cp artwork/szpont-detected.jpg $(ROOTFS_DIR)/usr/share/szpont-detected.png 2>/dev/null || true; fi
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
# ==============================================================================
$(ISO_IMAGE): $(KERNEL_ELF) $(BUILD_DIR)/initramfs.tar limine-bin/limine-bios.sys limine-bin/limine $(DISK_IMAGE)
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
