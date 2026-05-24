# m5-ctOS — Cardputer ADV Firmware

```
  ____ _____ ___  ____
 / ___|_   _/ _ \/ ___|
| |     | || | | \___ \
| |___  | || |_| |___) |
 \____| |_| \___/|____/   v2.0  ·  ESP32-S3 · M5Stack Cardputer ADV
```

A modular, extensible operating system for the **M5Stack Cardputer ADV**,
built on ESP-IDF 5.x. Modules are hot-loadable `.ctm` packages;
configuration and module upload happen through a built-in Wi-Fi web UI.

---

## Repository Layout

```
m5-ctos-cardputer/
├── firmware/               ← ESP-IDF project (the actual Cardputer OS)
│   ├── CMakeLists.txt
│   ├── idf_component.yml   ← managed dependencies (M5Unified, LittleFS, cJSON…)
│   ├── partitions.csv
│   ├── sdkconfig.defaults
│   ├── main/               ← application source
│   │   ├── ctOS_main.cpp   ← boot sequence
│   │   ├── modules/        ← loader, registry, manifest parser
│   │   ├── settings/       ← NVS config, PIN management
│   │   ├── ui/             ← menu, file browser, module manager, memory view
│   │   └── wifi/           ← hotspot, web server
│   ├── components/
│   │   └── cardputer_keyboard/  ← TCA8418 I2C keyboard driver
│   └── modules/            ← built-in .ctm module sources
│       ├── badusb/
│       ├── ble_hid_inject/
│       ├── blerp/
│       └── …
│
├── host/                   ← Dev tooling (runs on your Linux machine)
│   ├── tools/
│   │   ├── qa.py           ← master QA orchestrator
│   │   ├── agents/
│   │   │   ├── agent_correctness.py   ← static analysis (cppcheck)
│   │   │   ├── agent_security.py      ← security review (flawfinder + rules)
│   │   │   └── agent_boot_qa.py       ← host boot-sequence test runner
│   │   ├── linux-flash/
│   │   │   ├── flash.sh               ← auto-detect port, flash + logged monitor
│   │   │   ├── setup-fedora.sh        ← one-shot dev environment setup
│   │   │   └── 99-esp32.rules         ← udev rules (no sudo for serial)
│   │   └── package_module.py          ← .ctm packager
│   ├── tests/
│   │   ├── host/                      ← gcc-compiled Unity test suite
│   │   │   ├── boot_sim.c/h           ← host simulation of all 9 boot phases
│   │   │   ├── test_boot_sequence.c   ← 24 boot-phase unit tests
│   │   │   ├── mocks/                 ← stub ESP-IDF / FreeRTOS headers
│   │   │   └── Makefile
│   │   └── qa_report.md               ← latest QA report (auto-generated)
│   └── web/                           ← Web UI assets (served from Cardputer AP)
│
├── Makefile                ← top-level convenience targets
├── README.md
└── .gitignore
```

---

## Quick Start

### 1 — Set up the dev environment (Fedora / Nobara)

```bash
./host/tools/linux-flash/setup-fedora.sh
```

This installs ESP-IDF 5.x, esptool, pyserial, flawfinder and sets up udev rules.

### 2 — Build

```bash
make build
# equivalent to: cd firmware && idf.py build
```

### 3 — Flash + Monitor (with persistent logging)

Plug in the Cardputer via USB-C, then:

```bash
make flash
# equivalent to: ./host/tools/linux-flash/flash.sh all
```

Every monitor session is logged to `host/tools/linux-flash/logs/serial_<timestamp>.log`.

```bash
make log          # attach monitor only (no flash) — also logged
make flash-only   # flash only, no monitor
make flash-log    # flash + monitor, no rebuild
```

### 4 — Boot debug

The firmware emits numbered `[N/9]` checkpoints over serial:

```
I (xxx) ctOS: ==============================
I (xxx) ctOS:  m5-ctOS v2.0  boot sequence
I (xxx) ctOS: ==============================
I (xxx) ctOS: [1/9] NVS flash init...
I (xxx) ctOS: [2/9] PSRAM check...
I (xxx) ctOS: [3/9] config_init...
I (xxx) ctOS: [4/9] module_registry_init...
I (xxx) ctOS: [5/9] ui_menu_init (M5.begin + display)...
I (xxx) ctOS: [6/9] keyboard init (TCA8418 I2C probe)...
I (xxx) ctOS: [7/9] memory_view_init...
I (xxx) ctOS: [8/9] wifi AP check...
I (xxx) ctOS: [9/9] module_loader_autoload...
I (xxx) ctOS:  Boot complete!
```

The **last checkpoint printed** before a crash is the failed init step.

---

## QA Pipeline

Run all three agents without a device:

```bash
make qa
# Runs:  agent_correctness → agent_security → agent_boot_qa
# Output: host/tests/qa_report.md
```

| Agent | What it checks |
|-------|---------------|
| **Correctness** | cppcheck + Python static rules (unsafe strcpy, strtok thread-safety, double-free…) |
| **Security** | flawfinder + 11 project-specific rules (timing side-channels, credential hygiene, unchecked returns…) |
| **Boot Tests** | 24 Unity tests — compiled for host with gcc, no device needed |

### Run boot tests only

```bash
make test
# Builds + runs host/tests/host/ with gcc
```

Latest QA result: [`host/tests/qa_report.md`](host/tests/qa_report.md)

---

## Security Model

| Aspect | Implementation |
|--------|---------------|
| **PIN storage** | SHA-256 hash stored in NVS — plaintext never persisted |
| **PIN comparison** | Constant-time `ct_memcmp()` — prevents timing side-channel |
| **Default Wi-Fi password** | Derived from BT MAC (`ctOS-XXYYZZ!`) — unique per device, not readable from source |
| **Default SSID** | Derived from AP MAC (`ctOS-XXYYZZ`) |
| **Web UI upload** | PIN-gated; modules are stored on SD/LittleFS, not flashed |

> **First boot:** change your PIN and Wi-Fi password via the web UI at `192.168.4.1`.

---

## Module Catalog

| Module | Description |
|--------|-------------|
| `badusb` | USB HID keystroke injection (Rubber Ducky payloads) |
| `ble_hid_inject` | Bluetooth HID injection over NimBLE |
| `blerp` | BLE advertisement replay / spoof |
| `espnow_c2` | ESP-NOW command & control relay |
| `gairoscope` | Gyroscope-based data exfiltration demo |
| `ir_dazzle` | IR LED blaster / universal remote |
| `nuit_inject` | NTP-triggered payload launcher |
| `passive_keystroke` | Passive USB keystroke logger demo |
| `passive_wifi_csi` | Passive Wi-Fi CSI presence detection |
| `sonar_snoop` | Ultrasonic covert channel |
| `ult_jammer` | Ultrasonic disruption emitter |
| `wiki_eve` | Wi-Fi deauth + evil-twin captive portal |

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Run `make qa` before opening a PR.

---

## License

MIT — see `LICENSE`.
