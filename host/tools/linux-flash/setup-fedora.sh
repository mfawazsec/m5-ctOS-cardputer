#!/usr/bin/env bash
# setup-fedora.sh — Install ESP-IDF v5.x for m5-ctOS on Fedora / Nobara Linux
set -euo pipefail

IDF_VERSION="v5.3.1"
IDF_PATH="${HOME}/esp/esp-idf"

echo "==> m5-ctOS ESP-IDF setup for Fedora/Nobara"
echo "    IDF version : ${IDF_VERSION}"
echo "    IDF path    : ${IDF_PATH}"
echo ""

# ── System packages ──────────────────────────────────────────────────────────
echo "==> Installing system packages via dnf..."
sudo dnf install -y \
    git cmake ninja-build ccache \
    python3 python3-pip python3-devel python3-virtualenv \
    dfu-util \
    openssl-devel \
    libusb1-devel \
    libusbx-devel \
    gcc g++ \
    make \
    flex bison \
    gperf \
    wget curl \
    xxd

# ── USB permissions ───────────────────────────────────────────────────────────
echo ""
echo "==> Setting up USB serial permissions..."

# Add user to dialout group (needed for /dev/ttyUSB* and /dev/ttyACM*)
if ! groups "$USER" | grep -q dialout; then
    sudo usermod -a -G dialout "$USER"
    echo "    Added $USER to dialout group."
    echo "    *** You must log out and back in (or reboot) for this to take effect. ***"
else
    echo "    $USER already in dialout group."
fi

# Install udev rule for ESP32-S3 / Cardputer USB
UDEV_RULE='ATTRS{idVendor}=="303a", ATTRS{idProduct}=="1001", MODE="0666", GROUP="dialout"'
UDEV_FILE="/etc/udev/rules.d/99-esp32.rules"
if [ ! -f "$UDEV_FILE" ]; then
    echo "$UDEV_RULE" | sudo tee "$UDEV_FILE" > /dev/null
    sudo udevadm control --reload-rules
    sudo udevadm trigger
    echo "    udev rule installed: $UDEV_FILE"
else
    echo "    udev rule already present: $UDEV_FILE"
fi

# ── ESP-IDF ───────────────────────────────────────────────────────────────────
echo ""
echo "==> Installing ESP-IDF ${IDF_VERSION}..."
mkdir -p "${HOME}/esp"

if [ -d "${IDF_PATH}/.git" ]; then
    echo "    ESP-IDF already cloned — fetching and checking out ${IDF_VERSION}..."
    git -C "${IDF_PATH}" fetch --tags
    git -C "${IDF_PATH}" checkout "${IDF_VERSION}"
    git -C "${IDF_PATH}" submodule update --init --recursive
else
    git clone --branch "${IDF_VERSION}" --depth 1 --recurse-submodules \
        https://github.com/espressif/esp-idf.git "${IDF_PATH}"
fi

echo ""
echo "==> Running ESP-IDF install script for esp32s3..."
"${IDF_PATH}/install.sh" esp32s3

# ── Shell alias ───────────────────────────────────────────────────────────────
ALIAS_LINE="alias get_idf='. \${HOME}/esp/esp-idf/export.sh'"
SHELL_RC="${HOME}/.bashrc"
if [ -n "${ZSH_VERSION:-}" ] || [ "$(basename "$SHELL")" = "zsh" ]; then
    SHELL_RC="${HOME}/.zshrc"
fi

if ! grep -q "get_idf" "${SHELL_RC}" 2>/dev/null; then
    echo "" >> "${SHELL_RC}"
    echo "# ESP-IDF" >> "${SHELL_RC}"
    echo "${ALIAS_LINE}" >> "${SHELL_RC}"
    echo "    Added 'get_idf' alias to ${SHELL_RC}"
else
    echo "    'get_idf' alias already in ${SHELL_RC}"
fi

# ── Done ─────────────────────────────────────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  Setup complete!                                             ║"
echo "╠══════════════════════════════════════════════════════════════╣"
echo "║  Next steps:                                                 ║"
echo "║                                                              ║"
echo "║  1. Log out and back in (for dialout group to take effect)   ║"
echo "║  2. Open a new terminal and run:                             ║"
echo "║       get_idf                                                ║"
echo "║  3. From repo root:                                          ║"
echo "║       make build                                             ║"
echo "║  4. Plug in Cardputer via USB-C, then:                       ║"
echo "║       ./tools/linux-flash/flash.sh                           ║"
echo "╚══════════════════════════════════════════════════════════════╝"
