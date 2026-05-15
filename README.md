```
 ██████╗████████╗ ██████╗ ███████╗
██╔════╝╚══██╔══╝██╔═══██╗██╔════╝
██║        ██║   ██║   ██║███████╗
██║        ██║   ██║   ██║╚════██║
╚██████╗   ██║   ╚██████╔╝███████║
 ╚═════╝   ╚═╝    ╚═════╝ ╚══════╝
        m5-ctOS  v2.0
  Security Research OS · ESP32-S3
```

<div align="center">

[![CI](https://github.com/mfawazsec/m5-ctos-cardputer/actions/workflows/ci.yml/badge.svg?branch=claude/setup-m5-ctOS-build-YvCoW)](https://github.com/mfawazsec/m5-ctos-cardputer/actions)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.3.1-blue?logo=espressif)](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/)
[![Target](https://img.shields.io/badge/target-ESP32--S3-orange?logo=espressif)](https://www.espressif.com/en/products/socs/esp32-s3)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![Auth](https://img.shields.io/badge/use-authorized%20research%20only-red)](#-authorization--ethics)

**PIN-gated · WiFi AP + Web UI · Hot-loadable Modules · 8 MB PSRAM**

</div>

---

## What is this?

m5-ctOS is a security research operating system for the **M5Stack Cardputer ADV** — a pocket-sized ESP32-S3 device with a full QWERTY keyboard, TFT display, built-in mic, speaker, and IR LED. It boots a WiFi access point, serves a PIN-authenticated web dashboard, and lets you hot-load `.ctm` module bundles over the air or from an SD card.

Twelve research modules are included as buildable stubs, each tied to a published academic paper. All modules require **explicit written authorization** before use against any target.

---

## Hardware

| Component | Detail |
|-----------|--------|
| SoC | ESP32-S3FN8P (dual-core Xtensa LX7, 240 MHz) |
| Flash | 8 MB (OPI) |
| PSRAM | 8 MB (OPI, 80 MHz) |
| Display | 1.14" ST7789 TFT, 135×240, SPI |
| Keyboard | MEGA328P matrix via I2C |
| Microphone | SPM1423 MEMS (I2S PDM, GPIO CLK=41 DIN=40) |
| Speaker | NS4168 I2S amp (BCK=34, WS=33, DOUT=35) |
| IR LED | GPIO 44, RMT peripheral |
| USB | Native ESP32-S3 USB-OTG (303a:1001) |
| Storage | MicroSD via SDMMC |

---

## Base OS Features

```
┌─────────────────────────────────────────────────────────┐
│  Boot                                                   │
│  ├── NVS init + PSRAM probe                             │
│  ├── Config load (PIN hash, SSID, brightness)           │
│  ├── Module registry (mutex-protected, max 8 loaded)    │
│  ├── WiFi AP  ctOS-<MAC>  WPA2  192.168.4.1             │
│  └── HTTP web UI (PIN-gated cookie auth)                │
│                                                         │
│  Display UI  (M5Canvas sprite, ~30 fps)                 │
│  ├── [M] Module manager  — load / unload                │
│  ├── [F] File browser    — SD card navigator            │
│  ├── [W] WiFi toggle                                    │
│  └── Live heap + PSRAM bar                              │
│                                                         │
│  Web Dashboard  http://192.168.4.1/                     │
│  ├── /         — status + connected clients             │
│  ├── /modules  — list, upload .ctm bundle               │
│  └── /settings — SSID, PIN change, brightness          │
└─────────────────────────────────────────────────────────┘
```

**Security model**
- PIN stored as SHA-256 hash in NVS — never plaintext, never in source
- Web auth via short-lived cookie; PIN form before any other route
- `.ctm` bundle = ZIP(manifest.json + firmware.bin + README.md)
- PSRAM allocations tracked per module; freed on unload

---

## Module Catalog

| # | Module | Category | Research Basis |
|---|--------|----------|----------------|
| 1 | `passive_wifi_csi` | Sensing | WiFi CSI-based activity/gesture recognition |
| 2 | `blerp` | BLE | CI Re-Pairing Attack (NDSS 2026) |
| 3 | `wiki_eve` | WiFi | BFI keystroke inference via beamforming frames |
| 4 | `passive_keystroke` | Acoustic | Acoustic side-channel keystroke logging (SPM1423) |
| 5 | `nuit_inject` | Audio | NUIT — inaudible ultrasonic voice injection |
| 6 | `ult_jammer` | Audio | Ultrasonic microphone jamming (18–22 kHz) |
| 7 | `sonar_snoop` | Acoustic | SonarSnoop — sonar-based gesture tracking |
| 8 | `badusb` | USB | BadUSB / DuckyScript HID keyboard emulation |
| 9 | `ble_hid_inject` | BLE | BLE HID keyboard injection via NimBLE |
| 10 | `ir_dazzle` | IR | IR LED dazzle / camera blinding (RMT) |
| 11 | `gairoscope` | Audio | GAIROSCOPE — FSK exfil via speaker→gyroscope |
| 12 | `espnow_c2` | RF | ESP-NOW C2 — encrypted peer-to-peer command channel |

Each module ships with:
- `src/main.cpp` — real ESP-IDF implementation stub
- `manifest.json` — id, version, author, hardware requirements
- `README.md` — build + usage instructions
- `RESEARCH.md` — threat model, authorization context, paper citations

---

## Quick Start

### 1 — One-time toolchain setup (Fedora / Nobara)

```bash
git clone https://github.com/mfawazsec/m5-ctos-cardputer.git
cd m5-ctos-cardputer

chmod +x tools/linux-flash/setup-fedora.sh
./tools/linux-flash/setup-fedora.sh

# Then log out and back in (dialout group)
```

The setup script installs ESP-IDF v5.3.1, adds you to `dialout`, installs the udev rule for the Cardputer's USB VID/PID (`303a:1001`), and adds a `get_idf` shell alias.

### 2 — Activate ESP-IDF each session

```bash
get_idf
```

### 3 — Build

```bash
make build
# or: idf.py build
```

### 4 — Flash

Plug in the Cardputer via USB-C data cable. If not detected, hold **Fn + G** while plugging in to force ROM download mode.

```bash
# Build + flash + open serial monitor:
./tools/linux-flash/flash.sh

# Flash only (skip rebuild):
./tools/linux-flash/flash.sh flash-only

# Monitor only (Ctrl+] to exit):
./tools/linux-flash/flash.sh monitor

# Factory reset NVS (wipes PIN + WiFi settings):
./tools/linux-flash/flash.sh erase
```

Override the port: `PORT=/dev/ttyACM1 ./tools/linux-flash/flash.sh`

### 5 — Connect

```
WiFi SSID : ctOS-XXXXXX   (last 3 MAC bytes)
Password  : ctOS2024!     (change in /settings)
Web UI    : http://192.168.4.1/
Default PIN: 0000
```

---

## Building a Module

```bash
# Scaffold a new module:
make module MOD=my_module

# Build the main project (modules compile as components):
make build

# Package as .ctm bundle for OTA upload:
make package MOD=my_module
# → build/my_module-1.0.0.ctm
```

### Module entry point

```c
// modules/my_module/src/main.cpp
#include "module_api.h"

extern "C" esp_err_t module_main(const ctos_api_t *api)
{
    api->log("my_module", "Hello from my_module");
    api->display_print("my_module", "Running!");
    // ... allocate PSRAM, create FreeRTOS task, etc.
    return ESP_OK;
}
```

### manifest.json

```json
{
  "id": "my_module",
  "name": "My Module",
  "version": "1.0.0",
  "author": "you",
  "category": "research",
  "requires_hardware": ["wifi"],
  "min_os_version": "2.0.0",
  "entrypoint": "module_main"
}
```

---

## Repository Layout

```
m5-ctOS-cardputer/
├── main/                   # Base OS
│   ├── ctOS_main.cpp       # app_main entry
│   ├── settings/           # NVS config, PIN hash
│   ├── wifi/               # AP + HTTP web server
│   ├── modules/            # Registry, manifest, loader
│   └── ui/                 # Menu, file browser, module manager
├── modules/                # 12 research module stubs
│   ├── passive_wifi_csi/
│   ├── blerp/
│   ├── wiki_eve/
│   ├── passive_keystroke/
│   ├── nuit_inject/
│   ├── ult_jammer/
│   ├── sonar_snoop/
│   ├── badusb/
│   ├── ble_hid_inject/
│   ├── ir_dazzle/
│   ├── gairoscope/
│   └── espnow_c2/
├── web/                    # Web UI (HTML/JS, embedded in firmware)
├── tools/
│   ├── linux-flash/        # Fedora setup + flash scripts
│   ├── package_module.py   # .ctm bundle builder
│   └── implant/            # ESP-NOW implant (separate project)
├── components/             # Shared ESP-IDF components
├── CMakeLists.txt
├── sdkconfig.defaults      # ESP32-S3, PSRAM OCT 80MHz, NimBLE
├── partitions.csv          # NVS / factory / SPIFFS / modules FAT
├── idf_component.yml       # m5unified, cjson, nimble
└── Makefile
```

---

## Partition Layout

```
0x009000  nvs         (24 KB)   — PIN hash, WiFi config, autoload list
0x00f000  phy_init    (4 KB)
0x010000  factory     (3 MB)    — Base OS firmware
0x310000  storage     (1 MB)    — SPIFFS (web assets)
0x410000  modules     (3.75 MB) — FAT — hot-loaded .ctm bundles
```

---

## CI

GitHub Actions runs on every push to `dev` or `module/**` branches and on PRs to `main`:

| Job | Steps |
|-----|-------|
| **lint** | clang-format check, Python black, manifest JSON validation |
| **build** | `espressif/esp-idf-ci-action@v1` · target `esp32s3` · IDF v5.3.1 |
| **artifact** | Uploads `build/m5-ctOS.bin` + `build/m5-ctOS.elf` |

---

## Authorization & Ethics

> **All modules are for authorized security research and education only.**

- Never test against any device, network, or person without **explicit written authorization**
- IR dazzle: do not exceed 100 mA peak LED current
- Ultrasonic modules: comply with local RF/acoustic emissions regulations
- ESP-NOW C2: enable AES-128 PMK/LMK before any operational use
- BLE/WiFi attack modules: authorized lab/CTF environments only

Each `RESEARCH.md` contains the relevant threat model, authorization checklist, and academic citations.

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Branch model:

| Branch | Purpose |
|--------|---------|
| `main` | Stable releases |
| `dev` | Integration branch |
| `module/<name>` | New module development |
| `fix/<issue>` | Bug fixes |

---

## Cardputer USB Notes

| State | Device |
|-------|--------|
| Normal boot / running firmware | `/dev/ttyACM0` |
| ROM download mode | `/dev/ttyACM0` (same VID `303a:1001`) |

If the device isn't detected:
1. Use a USB-C cable that carries **data**, not charge-only
2. Hold **Fn + G** while plugging in to force ROM download mode
3. Check permissions: `ls -la /dev/ttyACM*` — should be `crw-rw-rw-`
4. Verify group: `groups $USER` should include `dialout`

---

<div align="center">

Built for the M5Stack Cardputer ADV · ESP-IDF v5.3.1 · FreeRTOS · M5Unified

</div>
