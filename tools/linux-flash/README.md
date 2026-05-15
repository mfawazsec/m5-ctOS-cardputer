# Linux Flash Setup (Fedora / Nobara)

Quick start for flashing m5-ctOS onto the M5Stack Cardputer ADV from a Fedora or Nobara Linux machine.

---

## One-time setup

```bash
# From repo root:
chmod +x tools/linux-flash/setup-fedora.sh
./tools/linux-flash/setup-fedora.sh
```

This script:
- Installs required dnf packages (cmake, ninja, python3, dfu-util, etc.)
- Clones ESP-IDF v5.3.1 into `~/esp/esp-idf`
- Runs `install.sh esp32s3`
- Adds you to the `dialout` group
- Installs the udev rule for the Cardputer USB VID/PID
- Adds `alias get_idf='. ~/esp/esp-idf/export.sh'` to your shell RC

**After setup: log out and back in** (dialout group requires a new login session).

---

## Every session

```bash
get_idf           # activate ESP-IDF in current shell
```

---

## Build and flash

```bash
# From repo root — build + flash + open serial monitor:
./tools/linux-flash/flash.sh

# Flash without rebuilding:
./tools/linux-flash/flash.sh flash-only

# Serial monitor only (Ctrl+] to exit):
./tools/linux-flash/flash.sh monitor

# Wipe flash (factory reset NVS):
./tools/linux-flash/flash.sh erase
```

The script auto-detects `/dev/ttyACM0` (native USB) or `/dev/ttyUSB0` (CH340).  
Override the port: `PORT=/dev/ttyACM1 ./tools/linux-flash/flash.sh`

---

## Cardputer USB notes

The Cardputer ADV uses ESP32-S3 native USB — no separate UART chip. On Linux it appears as:

| State | Device |
|-------|--------|
| Normal boot / running firmware | `/dev/ttyACM0` |
| ROM download mode | `/dev/ttyACM0` (same VID `303a:1001`) |

**If the device isn't detected:**
1. Use a USB-C cable that carries data, not charge-only
2. Hold **Fn + G** on the Cardputer keyboard while plugging in to force ROM download mode
3. Check permissions: `ls -la /dev/ttyACM*` — should be `crw-rw-rw-`
4. Verify group: `groups $USER` should include `dialout`
5. Temporary workaround: `sudo chmod a+rw /dev/ttyACM0`

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `Permission denied: /dev/ttyACM0` | Log out/in after `setup-fedora.sh`, or `sudo chmod a+rw /dev/ttyACM0` |
| `No serial ports found` | Check USB cable (data capable), try Fn+G boot |
| `idf.py: command not found` | Run `get_idf` in current terminal |
| Build fails: missing component | Run `idf.py add-dependency "m5stack/m5unified"` once, then rebuild |
| Flash succeeds but screen blank | Check `make monitor` output — likely a missing M5Unified component |
