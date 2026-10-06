# SzpontOS QEMU Emulator Execution Rules
# Included by root Makefile

.PHONY: run run-virtio run-virgl run-ps2 run-usb run-stress run-cli debug

# NOTE (single-shot fix): $(ISO_IMAGE) already depends on `build`,
# so listing both would re-introduce the parallel race. Depend only on ISO.
# Run in graphical QEMU
run: $(ISO_IMAGE)
	@./scripts/run_qemu.sh $(ISO_IMAGE)

# Run in graphical QEMU with Virtio-VGA
run-virtio: $(ISO_IMAGE)
	@./scripts/run_qemu.sh $(ISO_IMAGE) --virtio

# Run in graphical QEMU with Virtio-GPU 3D Virgl Acceleration
run-virgl: $(ISO_IMAGE)
	@./scripts/run_qemu.sh $(ISO_IMAGE) --virgl

# Run in graphical QEMU with Bare Metal PS/2 simulation
run-ps2: $(ISO_IMAGE)
	@./scripts/run_qemu.sh $(ISO_IMAGE) --baremetal-ps2

# Run in graphical QEMU with Pure USB xHCI (UEFI Modern Bare Metal)
run-usb: $(ISO_IMAGE)
	@./scripts/run_qemu.sh $(ISO_IMAGE) --baremetal-usb

# Run with realistic timing and instruction cycle stress test
run-stress: $(ISO_IMAGE)
	@./scripts/run_qemu.sh $(ISO_IMAGE) --timing-stress

# Run in headless QEMU (terminal only)
run-cli: $(ISO_IMAGE)
	@./scripts/run_qemu.sh $(ISO_IMAGE) --headless

# Run in graphical QEMU with GDB debug stub
debug: $(ISO_IMAGE)
	@./scripts/run_qemu.sh $(ISO_IMAGE) --debug
