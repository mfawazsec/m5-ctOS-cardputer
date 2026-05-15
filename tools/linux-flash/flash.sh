#!/usr/bin/env bash
# flash.sh — Auto-detect Cardputer USB port and flash m5-ctOS
#
# Usage:
#   ./tools/linux-flash/flash.sh              # build + flash + monitor
#   ./tools/linux-flash/flash.sh flash-only   # skip build
#   ./tools/linux-flash/flash.sh monitor      # monitor only
#   ./tools/linux-flash/flash.sh erase        # erase flash (factory reset)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IDF_PATH="${IDF_PATH:-${HOME}/esp/esp-idf}"
BAUD="${BAUD:-460800}"
MODE="${1:-all}"

# ── Source ESP-IDF if not already active ─────────────────────────────────────
if ! command -v idf.py &>/dev/null; then
    if [ -f "${IDF_PATH}/export.sh" ]; then
        echo "==> Sourcing ESP-IDF from ${IDF_PATH}..."
        # shellcheck source=/dev/null
        source "${IDF_PATH}/export.sh"
    else
        echo "ERROR: idf.py not found and ESP-IDF not at ${IDF_PATH}"
        echo "       Run: ./tools/linux-flash/setup-fedora.sh"
        exit 1
    fi
fi

# ── Detect USB port ───────────────────────────────────────────────────────────
detect_port() {
    # ESP32-S3 native USB appears as ttyACM*, CH340/CP210x as ttyUSB*
    local port=""

    # Prefer ttyACM0 (ESP32-S3 native USB — most likely for Cardputer)
    for candidate in /dev/ttyACM0 /dev/ttyACM1 /dev/ttyUSB0 /dev/ttyUSB1; do
        if [ -e "$candidate" ]; then
            port="$candidate"
            break
        fi
    done

    # If nothing found, scan all candidates
    if [ -z "$port" ]; then
        port=$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -1)
    fi

    echo "$port"
}

PORT="${PORT:-$(detect_port)}"

if [ -z "$PORT" ]; then
    echo ""
    echo "ERROR: No USB serial device found."
    echo ""
    echo "  Cardputer troubleshooting:"
    echo "  1. Make sure the USB-C cable supports data (not charge-only)"
    echo "  2. Hold Fn+G on the Cardputer keyboard while plugging in to enter"
    echo "     download mode (ESP32-S3 ROM bootloader)"
    echo "  3. Check: ls /dev/ttyACM* /dev/ttyUSB*"
    echo "  4. Check group: groups \$USER  (should include 'dialout')"
    echo "  5. Try: sudo chmod a+rw /dev/ttyACM0  (temporary fix)"
    echo ""
    exit 1
fi

echo "==> Using port: ${PORT}"
echo "==> Baud rate : ${BAUD}"
echo "==> Mode      : ${MODE}"
echo ""

cd "${REPO_ROOT}"

case "$MODE" in
    all)
        echo "==> Building..."
        idf.py build
        echo ""
        echo "==> Flashing to ${PORT}..."
        idf.py -p "${PORT}" -b "${BAUD}" flash
        echo ""
        echo "==> Starting monitor (Ctrl+] to exit)..."
        idf.py -p "${PORT}" monitor
        ;;

    flash-only)
        echo "==> Flashing to ${PORT} (no build)..."
        idf.py -p "${PORT}" -b "${BAUD}" flash
        ;;

    monitor)
        echo "==> Monitor on ${PORT} (Ctrl+] to exit)..."
        idf.py -p "${PORT}" monitor
        ;;

    erase)
        echo "==> Erasing flash on ${PORT}..."
        echo "    WARNING: This wipes all NVS data including PIN and WiFi settings."
        read -r -p "    Continue? [y/N] " confirm
        if [[ "$confirm" =~ ^[Yy]$ ]]; then
            esptool.py --port "${PORT}" --baud "${BAUD}" erase_flash
            echo "    Flash erased. Flash firmware again with: ./tools/linux-flash/flash.sh flash-only"
        else
            echo "    Aborted."
        fi
        ;;

    *)
        echo "Usage: $0 [all|flash-only|monitor|erase]"
        exit 1
        ;;
esac
