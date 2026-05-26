#!/usr/bin/env bash
# flash.sh — Auto-detect Cardputer USB port, flash m5-ctOS, and log serial output
#
# Usage:
#   ./host/tools/linux-flash/flash.sh              # build + flash + monitor (logged)
#   ./host/tools/linux-flash/flash.sh flash-only   # flash only, no monitor
#   ./host/tools/linux-flash/flash.sh flash-log    # flash-only + monitor (logged)
#   ./host/tools/linux-flash/flash.sh monitor      # monitor only (logged)
#   ./host/tools/linux-flash/flash.sh log          # alias for monitor
#   ./host/tools/linux-flash/flash.sh erase        # erase flash (factory reset)
#
# Serial output is always saved to:
#   host/tools/linux-flash/logs/serial_YYYY-MM-DD_HH-MM-SS.log
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"   # 3 levels up: linux-flash → tools → host → repo
FIRMWARE_DIR="${REPO_ROOT}/firmware"
IDF_PATH="${IDF_PATH:-${HOME}/esp/esp-idf}"
BAUD="${BAUD:-460800}"
MODE="${1:-all}"

# ── Log directory ──────────────────────────────────────────────────────────────
LOG_DIR="${SCRIPT_DIR}/logs"
mkdir -p "${LOG_DIR}"
LOG_FILE="${LOG_DIR}/serial_$(date '+%Y-%m-%d_%H-%M-%S').log"

# ── Source ESP-IDF if not already active ──────────────────────────────────────
if ! command -v idf.py &>/dev/null; then
    if [ -f "${IDF_PATH}/export.sh" ]; then
        echo "==> Sourcing ESP-IDF from ${IDF_PATH}..."
        # shellcheck source=/dev/null
        source "${IDF_PATH}/export.sh"
    else
        echo "ERROR: idf.py not found and ESP-IDF not at ${IDF_PATH}"
        echo "       Run: ./host/tools/linux-flash/setup-fedora.sh"
        exit 1
    fi
fi

# ── Detect USB port ───────────────────────────────────────────────────────────
detect_port() {
    local port=""
    for candidate in /dev/ttyACM0 /dev/ttyACM1 /dev/ttyUSB0 /dev/ttyUSB1; do
        if [ -e "$candidate" ]; then
            port="$candidate"
            break
        fi
    done
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

echo "==> Using port : ${PORT}"
echo "==> Baud rate  : ${BAUD}"
echo "==> Mode       : ${MODE}"
echo "==> Log file   : ${LOG_FILE}"
echo "==> Firmware   : ${FIRMWARE_DIR}"
echo ""

# ── Monitor wrapper: tee output to log file ───────────────────────────────────
run_monitor_logged() {
    local port="$1"
    echo "==> Starting monitor (Ctrl+] to exit) — logging to:"
    echo "    ${LOG_FILE}"
    echo ""
    if command -v script &>/dev/null; then
        script -q -c "idf.py -p '${port}' monitor" "${LOG_FILE}"
    else
        idf.py -p "${port}" monitor 2>&1 | tee "${LOG_FILE}"
    fi
    echo ""
    echo "==> Session ended. Log saved to:"
    echo "    ${LOG_FILE}"
}

# ── esptool.py write_flash with known-good settings for Cardputer ADV ────────
# ESP32-S3, 8 MB GD flash, DIO mode, 80 MHz — mirrors the working manual command.
do_flash() {
    local port="$1" baud="$2"
    local build="${FIRMWARE_DIR}/build"
    echo "==> Flashing to ${port} at ${baud} baud..."
    esptool.py \
        --chip esp32s3 \
        --port  "${port}" \
        --baud  "${baud}" \
        --before default_reset \
        --after  hard_reset \
        write_flash \
        --flash_mode dio \
        --flash_size 8MB \
        --flash_freq 80m \
        0x0     "${build}/bootloader/bootloader.bin" \
        0x8000  "${build}/partition_table/partition-table.bin" \
        0x10000 "${build}/m5-ctOS.bin"
}

# All idf.py commands run from the firmware directory
cd "${FIRMWARE_DIR}"

case "$MODE" in
    all)
        echo "==> Building..."
        idf.py build
        echo ""
        do_flash "${PORT}" "${BAUD}"
        echo ""
        run_monitor_logged "${PORT}"
        ;;

    flash-only)
        do_flash "${PORT}" "${BAUD}"
        ;;

    flash-log)
        echo "==> Flashing then monitoring..."
        do_flash "${PORT}" "${BAUD}"
        echo ""
        run_monitor_logged "${PORT}"
        ;;

    monitor|log)
        run_monitor_logged "${PORT}"
        ;;

    erase)
        echo "==> Erasing flash on ${PORT}..."
        echo "    WARNING: This wipes all NVS data including PIN and WiFi settings."
        read -r -p "    Continue? [y/N] " confirm
        if [[ "$confirm" =~ ^[Yy]$ ]]; then
            esptool.py --chip esp32s3 --port "${PORT}" --baud "${BAUD}" erase_flash
            echo "    Flash erased. Reflash with: ./host/tools/linux-flash/flash.sh flash-only"
        else
            echo "    Aborted."
        fi
        ;;

    *)
        echo "Usage: $0 [all|flash-only|flash-log|monitor|log|erase]"
        echo ""
        echo "  all        build + flash + monitor (with logging)"
        echo "  flash-only flash only, no monitor"
        echo "  flash-log  flash + monitor (with logging)"
        echo "  monitor    monitor only (with logging)"
        echo "  log        alias for monitor"
        echo "  erase      erase entire flash"
        echo ""
        echo "  Logs are saved to: host/tools/linux-flash/logs/"
        exit 1
        ;;
esac
