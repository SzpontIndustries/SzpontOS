#!/usr/bin/env bash
set -e

ISO_PATH="build/szpontos.iso"
DISPLAY_OPT="cocoa"
EXTRA_FLAGS=()
VGA_FLAGS=("-vga" "std" "-global" "VGA.vgamem_mb=64" "-global" "VGA.xres=2560" "-global" "VGA.yres=1440")
MACHINE_OPT="q35,i8042=on"
ENABLE_USB_KBD=true

# Detect Host OS
OS_TYPE="$(uname -s)"
if [ "$OS_TYPE" == "Darwin" ]; then
    DISPLAY_OPT="cocoa"
    CPU_TYPE="max"
else
    # Linux / BSD
    DISPLAY_OPT="default"
    # Check for KVM hardware acceleration on Linux
    if [ -w /dev/kvm ]; then
        echo "[*] Włączono sprzętową akcelerację KVM na Linuxie (-enable-kvm -cpu host)"
        EXTRA_FLAGS+=("-enable-kvm")
        CPU_TYPE="host"
    else
        CPU_TYPE="qemu64"
    fi
fi

# Parse arguments
for arg in "$@"; do
    case "$arg" in
        --debug)
            echo "[*] Tryb debugowania GDB włączony (QEMU czeka na porcie localhost:1234)..."
            EXTRA_FLAGS+=("-s" "-S")
            ;;
        --headless|--cli|-nographic)
            DISPLAY_OPT="none"
            ;;
        --virtio)
            VGA_FLAGS=("-vga" "virtio" "-global" "virtio-vga.xres=2560" "-global" "virtio-vga.yres=1440")
            ;;
        --baremetal-ps2|--ps2)
            echo "[*] Tryb Bare Metal PS/2: Włączono kontroler i8042 PS/2 na płycie Q35"
            MACHINE_OPT="q35,i8042=on"
            ENABLE_USB_KBD=false
            ;;
        --baremetal-ehci|--ehci)
            echo "[*] Tryb Bare Metal EHCI USB 2.0: Włączono kontroler usb-ehci"
            ENABLE_USB_KBD=true
            ;;
        --baremetal-usb|--usb)
            echo "[*] Tryb Bare Metal USB: Wyłączono i8042 (czysty natywny USB xHCI / UEFI)"
            MACHINE_OPT="q35,i8042=off"
            ENABLE_USB_KBD=true
            ;;
        --timing-stress|--realistic-timing)
            echo "[*] Włączono realistyczne zegary i wirtualny licznik instrukcji (-icount shift=auto)"
            EXTRA_FLAGS+=("-icount" "shift=auto,sleep=on" "-rtc" "base=utc,clock=vm")
            ;;
        --no-shutdown)
            EXTRA_FLAGS+=("-no-shutdown")
            ;;
        *.iso)
            ISO_PATH="$arg"
            ;;
    esac
done

if [ ! -f "$ISO_PATH" ]; then
    echo "[!] Nie znaleziono obrazu ISO: $ISO_PATH"
    echo "[*] Budowanie obrazu ISO..."
    make iso
fi

QEMU_CMD="qemu-system-x86_64"
if ! command -v $QEMU_CMD >/dev/null 2>&1; then
    if [ -x "/opt/homebrew/bin/qemu-system-x86_64" ]; then
        QEMU_CMD="/opt/homebrew/bin/qemu-system-x86_64"
    elif [ -x "/usr/bin/qemu-system-x86_64" ]; then
        QEMU_CMD="/usr/bin/qemu-system-x86_64"
    else
        echo "[!] Błąd: Nie znaleziono qemu-system-x86_64. Zainstaluj QEMU."
        exit 1
    fi
fi

DISK_PATH="build/disk.img"
if [ ! -f "$DISK_PATH" ]; then
    echo "[*] Generowanie obrazu dysku ext2: $DISK_PATH..."
    python3 scripts/make_ext2_disk.py "$DISK_PATH"
fi

if [ -f "$DISK_PATH" ]; then
    EXTRA_FLAGS+=("-drive" "file=$DISK_PATH,format=raw,if=none,id=disk0,file.locking=off" "-device" "ide-hd,drive=disk0,bus=ide.0")
fi



# Intel E1000 Network Card with User-mode NAT and port forwarding
HTTP_PORT=8080
while lsof -Pi :${HTTP_PORT} -sTCP:LISTEN -t >/dev/null 2>&1; do
    HTTP_PORT=$((HTTP_PORT + 1))
done
EXTRA_FLAGS+=("-netdev" "user,id=net0,hostfwd=tcp::${HTTP_PORT}-:80" "-device" "e1000,netdev=net0")


# xHCI USB 3.0 & EHCI USB 2.0 Host Controllers with USB HID Devices
EXTRA_FLAGS+=("-device" "qemu-xhci,id=xhci" "-device" "usb-ehci,id=ehci")
if [ "$ENABLE_USB_KBD" = true ]; then
    EXTRA_FLAGS+=("-device" "usb-kbd,bus=xhci.0" "-device" "usb-mouse,bus=xhci.0")
fi


exec $QEMU_CMD \
    -M "$MACHINE_OPT" \
    -cpu "$CPU_TYPE" \
    -m 512M \
    "${VGA_FLAGS[@]}" \
    -display "$DISPLAY_OPT" \
    -cdrom "$ISO_PATH" \
    -serial stdio \
    "${EXTRA_FLAGS[@]}"
