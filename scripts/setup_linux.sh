#!/usr/bin/env bash
set -e

echo "=== Konfiguracja środowiska budowania SzpontOS na Linuxie ==="

# Check distribution / package manager
if [ -f /etc/os-release ]; then
    . /etc/os-release
    DISTRO=$ID
    DISTRO_LIKE=$ID_LIKE
else
    DISTRO="unknown"
    DISTRO_LIKE="unknown"
fi

echo "[*] Wykryto dystrybucję: $DISTRO ($DISTRO_LIKE)"

if command -v apt-get >/dev/null 2>&1; then
    echo "[*] Instalowanie pakietów przez apt (Debian/Ubuntu/Mint)..."
    sudo apt-get update
    sudo apt-get install -y \
        build-essential \
        gcc \
        g++ \
        gcc-x86-64-linux-gnu \
        binutils \
        nasm \
        xorriso \
        qemu-system-x86 \
        bear \
        git \
        python3 \
        make \
        gdb \
        cmake \
        clang \
        autoconf \
        automake \
        libtool \
        pkg-config \
        xutils-dev \
        imagemagick

elif command -v dnf >/dev/null 2>&1; then
    echo "[*] Instalowanie pakietów przez dnf (Fedora/RHEL)..."
    sudo dnf install -y \
        gcc \
        gcc-c++ \
        binutils \
        nasm \
        xorriso \
        qemu-system-x86 \
        bear \
        git \
        python3 \
        make \
        gdb \
        cmake \
        clang \
        autoconf \
        automake \
        libtool \
        pkgconf-pkg-config \
        xorg-x11-util-macros \
        ImageMagick

elif command -v pacman >/dev/null 2>&1; then
    echo "[*] Instalowanie pakietów przez pacman (Arch/Manjaro)..."
    sudo pacman -Syu --noconfirm \
        base-devel \
        gcc \
        binutils \
        nasm \
        xorriso \
        qemu-system-x86 \
        bear \
        git \
        python \
        make \
        gdb \
        cmake \
        clang \
        autoconf \
        automake \
        libtool \
        pkgconf \
        xorg-util-macros \
        imagemagick

elif command -v zypper >/dev/null 2>&1; then
    echo "[*] Instalowanie pakietów przez zypper (openSUSE)..."
    sudo zypper install -y \
        gcc \
        gcc-c++ \
        binutils \
        nasm \
        xorriso \
        qemu-x86 \
        bear \
        git \
        python3 \
        make \
        gdb \
        cmake \
        clang \
        autoconf \
        automake \
        libtool \
        pkg-config \
        xorg-x11-util-devel \
        ImageMagick

elif command -v apk >/dev/null 2>&1; then
    echo "[*] Instalowanie pakietów przez apk (Alpine)..."
    sudo apk add \
        build-base \
        gcc \
        binutils \
        nasm \
        xorriso \
        qemu-system-x86_64 \
        bear \
        git \
        python3 \
        make \
        gdb \
        cmake \
        clang \
        autoconf \
        automake \
        libtool \
        pkgconf \
        util-macros \
        imagemagick
else
    echo "[!] Nie rozpoznano menedżera pakietów. Zainstaluj ręcznie: gcc, nasm, xorriso, qemu-system-x86_64, bear, make, python3, cmake, clang, autoconf, automake, libtool, pkg-config, xorg-macros (xutils-dev), imagemagick."
fi

# Optional KVM group permission check
if [ -e /dev/kvm ]; then
    if [ ! -w /dev/kvm ]; then
        echo "[*] Dodawanie użytkownika $USER do grupy kvm (dla akceleracji sprzętowej QEMU KVM)..."
        sudo usermod -aG kvm "$USER" 2>/dev/null || sudo usermod -aG libvirt "$USER" 2>/dev/null || true
        echo "[!] Uwaga: Aby uprawnienia KVM zaczęły działać, może być wymagane ponowne zalogowanie."
    else
        echo "[OK] Akceleracja sprzętowa KVM jest dostępna dla bieżącego użytkownika."
    fi
fi

echo ""
echo "=== Środowisko Linux gotowe! ==="
echo "Możesz teraz uruchomić: make run"
