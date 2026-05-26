# m5-ctOS: Cardputer ADV Firmware

```
  ____ _____ ___  ____
 / ___|_   _/ _ \/ ___|
| |     | || | | \___ \
| |___  | || |_| |___) |
 \____| |_| \___/|____/   v2.0  -  ESP32-S3 - M5Stack Cardputer ADV
```

A modular, extensible operating system for the **M5Stack Cardputer ADV**,
built on ESP-IDF 5.x. All 12 modules are statically compiled into the firmware
and available immediately. Configuration and future module upload happen through
a built-in Wi-Fi web UI at `192.168.4.1`.

> **Status (2026-05-27):** All 12 modules confirmed working — boot, UI navigation,
> and functional flows verified via serial key injection (see below).
> Free heap at boot: ~19 KB. Zero crashes across full UI sweep.

---

## Repository Layout

```
m5-ctos-cardputer/
+-- firmware/               ESP-IDF project (the actual Cardputer OS)
|   +-- CMakeLists.txt
|   +-- idf_component.yml   managed dependencies (M5Unified, LittleFS, cJSON...)
|   +-- partitions.csv
|   +-- sdkconfig.defaults
|   +-- main/               application source
|   |   +-- ctOS_main.cpp   boot sequence (9 stages)
|   |   +-- modules/        loader, registry, manifest parser, module_log
|   |   +-- settings/       NVS config, PIN management
|   |   +-- ui/             menu, file browser, module manager, mod_common
|   |   +-- wifi/           hotspot (SoftAP), web server (HTTP)
|   +-- components/
|   |   +-- cardputer_keyboard/  TCA8418 I2C keyboard driver
|   +-- modules/            statically-compiled module sources
|       +-- badusb/
|       +-- ble_hid_inject/
|       +-- blerp/
|       +-- espnow_c2/
|       +-- gairoscope/
|       +-- ir_dazzle/
|       +-- nuit_inject/
|       +-- passive_keystroke/
|       +-- passive_wifi_csi/
|       +-- sonar_snoop/
|       +-- ult_jammer/
|       +-- wiki_eve/
|
+-- host/                   Dev tooling (runs on your Linux machine)
|   +-- tools/
|   |   +-- qa.py           master QA orchestrator
|   |   +-- agents/
|   |   |   +-- agent_correctness.py    static analysis (cppcheck)
|   |   |   +-- agent_security.py       security review (flawfinder + rules)
|   |   |   +-- agent_boot_qa.py        host boot-sequence test runner
|   |   +-- linux-flash/
|   |   |   +-- flash.sh                auto-detect port, flash + logged monitor
|   |   |   +-- setup-fedora.sh         one-shot dev environment setup
|   |   |   +-- 99-esp32.rules          udev rules (no sudo for serial)
|   |   +-- package_module.py           .ctm packager
|   +-- tests/
|   |   +-- host/                       gcc-compiled Unity test suite
|   |   |   +-- boot_sim.c/h            host simulation of all 9 boot phases
|   |   |   +-- test_boot_sequence.c    24 boot-phase unit tests
|   |   |   +-- mocks/                  stub ESP-IDF / FreeRTOS headers
|   |   |   +-- Makefile
|   |   +-- qa_report.md                latest QA report (auto-generated)
|   +-- web/                            Web UI assets (served from Cardputer AP)
|
+-- Makefile                top-level convenience targets
+-- README.md
+-- .gitignore
```

---

## Quick Start

### 1 - Set up the dev environment (Fedora / Nobara)

```bash
./host/tools/linux-flash/setup-fedora.sh
```

This installs ESP-IDF 5.x, esptool, pyserial, flawfinder and sets up udev rules.

### 2 - Build

```bash
make build
# equivalent to: cd firmware && idf.py build
```

Or directly with idf.py:

```bash
cd firmware
. ~/esp/esp-idf/export.sh
idf.py build
```

### 3 - Flash + Monitor

Plug in the Cardputer via USB-C, then:

```bash
make flash
# equivalent to: ./host/tools/linux-flash/flash.sh all
```

Or directly:

```bash
. ~/esp/esp-idf/export.sh
idf.py -p /dev/ttyACM0 flash
```

Every monitor session is logged to `host/tools/linux-flash/logs/serial_<timestamp>.log`.

```bash
make log          # attach monitor only (no flash), also logged
make flash-only   # flash only, no monitor
make flash-log    # flash + monitor, no rebuild
```

### 4 - Boot debug

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

The last checkpoint printed before a crash is the failed init step.

---

## Navigation

The Cardputer keyboard has no arrow keys. ctOS maps the QWERTY layout:

| Key | Action |
|-----|--------|
| `,` or `;` | Move cursor up |
| `.` or `/` | Move cursor down |
| `Enter` | Select / open |
| `` ` `` or `Esc` | Back / exit |
| `K` | Kill running module |
| `L` | Open verbose log view (inside a module) |

---

## Installing Modules

### Pre-installed modules (all 12)

All modules are statically compiled into the firmware and registered automatically
at boot. No SD card or file upload is required. From the main menu, press `Enter`
on "Modules" to open the Module Manager. Select a module with `,`/`.`, press
`Enter` to open its interactive UI screen.

### Uploading additional modules via the web UI

Future or custom modules can be packaged as `.ctm` files (a directory containing
a `manifest.json` and any supporting files) and uploaded through the web UI:

1. Connect to the Cardputer's Wi-Fi AP: SSID `ctOS-XXYYZZ` (XX/YY/ZZ are the
   last 3 bytes of the AP MAC address)
2. Open `http://192.168.4.1` in a browser
3. Enter your PIN (default shown on the Cardputer display at boot)
4. Use the "Upload Module" section to upload a `.ctm` archive
5. The module is unpacked to `/sdcard/modules/` or `/modules/` (FAT partition)
   and available after restart

### Module manifest format

Each module needs a `manifest.json`:

```json
{
  "id":       "my_module",
  "name":     "My Module Display Name",
  "version":  "1.0.0",
  "author":   "you",
  "category": "wifi"
}
```

### Writing a module

A module is a C/C++ source file exporting two functions:

```c
// Called once to start the module's background task
esp_err_t my_module_main(const ctos_api_t *api);

// Blocking UI function - runs in the calling task context
void my_module_ui_show(void);
```

The `ctos_api_t` struct provides:
- `api->log(id, msg)`: write to the per-module ring buffer log
- `api->display_print(id, line)`: write a line to the log view
- `api->psram_alloc(size)` / `api->psram_free(ptr)`: PSRAM allocation
- `api->get_free_psram()` / `api->get_free_heap()`: memory stats

Add the source to `firmware/main/CMakeLists.txt` under `SRCS`, add the extern
declarations and entries to `firmware/main/modules/loader.cpp`, and add a
manifest entry to `s_builtin_manifests[]`.

---

## Module Reference

### badusb - USB HID Keystroke Injection

Executes DuckyScript payload files (`.ducky` extension) stored on the SD card
at `/sdcard/payloads/`. The Cardputer enumerates as a USB HID keyboard via
the ESP32-S3 native USB OTG peripheral. The module lists all `.ducky` files,
lets you select one with `,`/`.`, and executes it with `Enter`.

DuckyScript commands supported: `STRING`, `DELAY`, `ENTER`, `TAB`, `CTRL`,
`GUI`, `ALT`, `DEFAULT_DELAY`, `#` (comment).

**Keys:** `,`/`.` navigate list - `Enter` execute - `R` reload list from SD - `L` log - `` ` `` back

**Requires:** USB connection to target host. SD card with `.ducky` files at `/sdcard/payloads/`.

**How it works:** `hid_send_key()` sends an 8-byte USB HID keyboard report
(modifier byte + 6 keycode bytes) over the USB OTG interface. Currently stubbed
to log; full tinyUSB HID device mode requires enabling `CONFIG_USB_OTG_SUPPORTED`
and linking the tinyUSB HID device component.

---

### ble_hid_inject - Bluetooth HID Wireless Keyboard Injection

Advertises the Cardputer as a Bluetooth HID keyboard named "ctOS Keyboard" using
NimBLE (IDF built-in). When a host connects and pairs, the module can inject
arbitrary keystrokes over BLE. The module also scans for nearby BLE devices and
displays them as potential targets.

**Keys:** `Enter` inject demo string - `,`/`.` navigate target list - `L` log - `` ` `` back

**How it works:** Implements a GATT HID service (UUID 0x1812) with a Report Map
characteristic (0x2A4B, standard boot keyboard descriptor) and an Input Report
characteristic (0x2A4D, notify). Keystrokes are sent as 8-byte HID reports via
`ble_gattc_notify_custom()`. Pairing uses the NimBLE GAP advertising path with
`BLE_GAP_CONN_MODE_UND`.

---

### blerp - BLE Re-Pairing / Confused Identity Attack

Scans for BLE devices (10-second scan windows), then attempts a "confused
identity" (CI) re-pairing attack against each discovered device. The attack
connects and immediately calls `ble_gap_security_initiate()`, attempting to
force the remote device to accept a new pairing. Devices that accept are logged
as "VULNERABLE"; devices that reject are logged as "protected". Results are
also written to `/sdcard/blerp_log.txt`.

**Keys:** `L` log - `` ` `` back

**How it works:** Uses NimBLE C API. On `BLE_GAP_EVENT_CONNECT`, calls
`ble_gap_security_initiate(conn_handle)`. On `BLE_GAP_EVENT_ENC_CHANGE`, checks
`ev->enc_change.status`: 0 means the pairing was accepted (vulnerable). The
attack exploits devices that do not require user confirmation for re-pairing.

---

### espnow_c2 - ESP-NOW Covert Command and Control

Uses ESP-NOW (802.11-layer peer-to-peer, no AP association required) as a
covert C2 channel. On startup, broadcasts a beacon packet (`ctOS-C2-BEACON`)
to all peers. Receives incoming ESP-NOW packets on any channel and displays
them. Useful for communicating with other ESP32 nodes without any network
infrastructure.

**Keys:** `B` send beacon - `C` cycle Wi-Fi channel (1-13) - `L` log - `` ` `` back

**How it works:** Initialises the Wi-Fi stack in STA mode if not already
running (checks `esp_wifi_get_mode()` first to avoid double-init). Registers
send and receive callbacks with `esp_now_register_send_cb` /
`esp_now_register_recv_cb`. All received packets are pushed to a FreeRTOS
queue (8 entries deep) processed by the background task.

---

### gairoscope - Speaker-to-Gyroscope Covert Channel

Implements the GAIROSCOPE attack: drives the internal speaker at specific
resonant frequencies (18-20 kHz) that cause the MEMS gyroscope to vibrate
sympathetically. Gyroscope readings modulated by the acoustic signal can be
read by a co-located phone app, creating a covert exfiltration channel.

**Keys:** `Space` toggle transmission on/off - `L` log - `` ` `` back

**How it works:** Uses M5Unified's `Speaker.tone()` to emit ultrasonic tones.
Frequency sweeps between 18,000 Hz and 20,000 Hz encoding binary data (FSK-style:
18 kHz = 0, 20 kHz = 1). The receiving end reads the phone's gyroscope via a
browser or app and demodulates the frequency shifts.

---

### ir_dazzle - IR Camera Dazzling

Drives the IR LED on the Cardputer ADV in three modes: continuous burst,
pulsed burst, and frequency sweep. Can temporarily saturate IR-sensitive
cameras (security cameras, night-vision devices) by flooding the sensor with
infrared light.

**Keys:** `1` continuous - `2` pulsed burst - `3` frequency sweep - `Space` toggle on/off - `L` log - `` ` `` back

**How it works:** Uses `ledc_timer_config` and `ledc_channel_config` on the IR
LED GPIO (GPIO 44 on Cardputer ADV). In sweep mode, steps through frequencies
from 30 kHz to 56 kHz to defeat notch-filter countermeasures. The background
task loops at the configured mode while `s_active` is true.

---

### nuit_inject - Near-Ultrasound Voice Injection

Implements the NUIT (Near-Ultrasound Inaudible Trojan) attack: encodes voice
commands as near-ultrasonic audio (17-20 kHz) played through the Cardputer
speaker. Microphones in nearby smart speakers, phones, or voice assistants
can demodulate the signal and interpret it as a voice command.

Includes 5 preset commands: wake-word trigger, volume increase, a silent URL
call, a phone call, and a smart-home toggle command.

**Keys:** `,`/`.` select command - `Enter` inject selected - `1`-`5` quick select - `L` log - `` ` `` back

**How it works:** Uses M5Unified speaker output with AM (amplitude modulation)
at an 18-20 kHz carrier. The voice command waveform is multiplied by the
carrier before playback, shifting the audio spectrum into the near-ultrasonic
range. The output is inaudible to humans but within the passband of many MEMS
microphones.

---

### passive_keystroke - Passive Acoustic Keystroke Logger

Captures audio from the internal SPM1423 PDM microphone (GPIO 40/41) and
detects keystrokes by their acoustic energy signature. When energy exceeds the
configurable threshold, the 512-sample window is saved as a WAV file at
`/sdcard/keystrokes/key_NNNNN.wav` for offline ML analysis.

**Keys:** `+`/`-` adjust energy threshold - `R` reset keystroke counter - `L` log - `` ` `` back

**UI shows:** live energy bar graph, current threshold, keystroke count, raw
energy value.

**How it works:** Uses the ESP-IDF I2S PDM RX driver at 16 kHz sample rate.
Each 512-sample frame is RMS-energy analysed. Rising-edge detection (above
threshold and was below) prevents multiple triggers per keystroke. WAV files
use the standard 44-byte RIFF/WAVE header for compatibility with all audio tools.

---

### passive_wifi_csi - Passive Wi-Fi Presence Detection (CSI)

Uses Wi-Fi Channel State Information (CSI) to passively detect motion and
presence in a room without any active transmission. The CSI data from incoming
802.11 packets encodes the multipath propagation environment; changes in that
environment (a person moving) appear as changes in the CSI amplitude matrix.

**Keys:** `Space` toggle capture on/off - `L` log - `` ` `` back

**UI shows:** live CSI amplitude bar graph across subcarriers, frame count.

**How it works:** Calls `esp_wifi_set_csi(true)` and
`esp_wifi_set_csi_rx_cb()` after starting Wi-Fi in STA mode (promiscuous not
required for CSI). Each CSI callback receives a `wifi_csi_info_t` struct
containing amplitude data for all OFDM subcarriers. The module computes the
L2 norm across subcarriers for display.

---

### sonar_snoop - Acoustic Gesture / PIN Inference

Emits a 20 kHz ultrasonic ping through the speaker and listens for the echo
via the PDM microphone. Changes in echo energy relative to a calibrated
baseline indicate hand/finger proximity and motion patterns, which can be used
to infer PINs typed on a nearby touchscreen or ATM pad.

**Keys:** `R` recalibrate baseline - `L` log - `` ` `` back

**UI shows:** echo delta bar graph, baseline energy, ping count.

**How it works:** Uses two I2S channels: I2S_NUM_1 (STD mode) for the speaker
output and I2S_NUM_0 (PDM RX mode) for microphone capture. A 5 ms 20 kHz
sine burst is transmitted, then the next 20 ms of audio is captured and
passed through a bandpass IIR filter centred at 20 kHz. The RMS energy of the
filtered response is compared against a 10-ping running average baseline.

---

### ult_jammer - Ultrasonic Microphone Jammer

Emits high-frequency broadband noise through the Cardputer's internal speaker
to jam nearby MEMS microphones. Operates in three intensity levels affecting
the signal amplitude. At high intensity, can prevent voice assistants and
recording devices within ~1 metre from picking up intelligible audio.

**WARNING:** High intensity may cause speaker distortion and is not recommended
for extended use.

**Keys:** `1`/`2`/`3` set intensity - `Space` toggle on/off - `L` log - `` ` `` back

**How it works:** Uses M5Unified `Speaker.tone()` at 20-21 kHz with amplitude
controlled by the `s_intensity` level (25%, 60%, 100% of max output). The
background task emits noise while `s_jammer_on` is true. The frequency is
within the passband of most MEMS microphones but inaudible to most humans.

---

### wiki_eve - Wi-Fi BFI Keystroke Inference (WiKI-Eve)

Implements the WiKI-Eve attack (2023): captures Wi-Fi 802.11ac Beamforming
Feedback Information (BFI) frames from the air. BFI frames contain channel
sounding data that encodes physical layer signal characteristics. When a
user types on a phone connected to the same AP, the keystrokes correlate with
specific BFI patterns that can be used to infer the typed characters with
high accuracy using ML models.

This module handles the capture side. BFI frames are buffered in PSRAM (64 KB)
and flushed to `/sdcard/bfi_capture.bin` for offline analysis.

**Keys:** `Space` toggle capture - `F` flush buffer to SD now - `C` change channel - `L` log - `` ` `` back

**UI shows:** BFI frame count, PSRAM buffer fill, SD card status.

**How it works:** Puts Wi-Fi in promiscuous mode (`esp_wifi_set_promiscuous(true)`)
filtered to management frames only (`WIFI_PROMIS_FILTER_MASK_MGMT`). The RX
callback checks for VHT Action frames with category 0x15 / action 0x00 (the
WLAN BFI action frame subtype). Each matching frame is length-prefixed and
written into a PSRAM ring buffer. Auto-flush triggers at 75% buffer fill.

---

## Technical Architecture

### Boot sequence (9 stages)

The `app_main()` in `ctOS_main.cpp` runs these stages in order:

1. **NVS init**: `nvs_flash_init()` with auto-erase on corruption
2. **PSRAM check**: reports available size via `esp_psram_get_size()`
3. **Config init**: loads NVS-backed settings (SSID, password, PIN hash, brightness, autoload list); also mounts SPIFFS at `/spiffs` and the FAT "modules" partition at `/modules`
4. **Module registry + loader**: `module_registry_init()` zeros the registry; `module_loader_init()` registers all 12 built-in modules and scans `/modules` and `/sdcard/modules` for extras
5. **Display**: `M5.begin()` inside `ui_menu_init()` initialises the M5Unified stack, sets display brightness and text size
6. **Keyboard**: `CardputerKb.init()` probes the TCA8418 at I2C address 0x34; continues without keyboard if not found
6b. **Serial inject task**: installs the USB JTAG driver and starts `serial_inject_task` (2 KB stack, priority 3) — prints `Key injector ready` to serial when live
7. **Memory view**: initialises the memory statistics screen
8. **Wi-Fi AP**: starts SoftAP and HTTP server if enabled in config
9. **Autoload**: calls `module_loader_start()` for each module ID in the NVS autoload list

### Module system

**Registry** (`registry.cpp`): fixed array of `module_info_t[MAX_LOADED_MODULES=16]` structs protected by a FreeRTOS mutex. Each entry holds id, name, version, author, category, running flag, and function pointers `run_fn` / `ui_fn`.

**Loader** (`loader.cpp`): owns the `ctos_api_t` instance passed to all modules. At init, parses 12 built-in JSON manifests with `manifest_parse()`, looks up the corresponding `run_fn`/`ui_fn` from `s_builtin_fns[]`, and calls `module_registry_add()`. `module_loader_start(id)` calls `info.run_fn(&s_api)` directly (no wrapper task).

**Module entry points**: each module exports `<id>_main(const ctos_api_t*)` which spawns a FreeRTOS background task and returns immediately, and `<id>_ui_show()` which is a blocking UI loop called from the module manager. Modules check `module_registry_is_running(ID)` in their task loops to know when to stop.

**Module log** (`module_log.cpp`): per-module ring buffer of 32 lines x 64 characters. `module_log_push(id, line)` is called by both `api_log` and `api_display_print`. The log view (`mod_show_log_view()` in `mod_common.cpp`) renders up to 8 lines at a time with `,`/`.` scrolling.

### Keyboard driver (TCA8418)

Custom component in `firmware/components/cardputer_keyboard/`. The TCA8418 is
an I2C keypad controller at address 0x34 on the internal I2C bus. The driver:
- Configures a 7-row x 8-column key matrix (56 keys)
- Sets event mode: keypress events pushed to an 10-entry on-chip FIFO
- On `update()`, drains the FIFO and maps key events to ASCII via a lookup table
- Exposes `isChange()`, `isPressed()`, `getState()` to match the old M5Cardputer API shape

The keymap is derived from the reference M5Stack Cardputer ADV launcher. The
TCA8418 uses a 10-column stride in its event code (row * 10 + col), requiring
a mask and stride correction in the event decoder.

**Serial key injection:** `cardputer_kb_inject_char(ch)` queues a virtual
keypress consumed on the next `update()` call. A background task reads bytes
from the USB JTAG serial interface (`/dev/ttyACM0`) and injects them — this
allows full remote UI testing without a physical keyboard:

```python
import serial
s = serial.Serial('/dev/ttyACM0', 115200)
s.write(b'.\n')   # down, enter — navigate and open a module
s.write(b'`')     # back to previous screen
```

The inject task paces injections at 220 ms intervals to match the UI's 250 ms
render timer + 150 ms debounce cycle.

### Display

240 x 135 pixel IPS LCD, driven by M5Unified. Text is set to size 1.5 (approx
9px character width, 12px line height with `MOD_LH = 12`), giving a maximum of
26 characters per line. Each module UI uses a **250 ms render timer** — the
display is only redrawn when the timer fires or a key is pressed, eliminating
flicker while keeping navigation responsive. All footer/status strings are
kept to ≤26 chars to fit within the 240px display width at this text size.

### Storage layout

| Partition | Type | Offset | Size | Mount point | Use |
|-----------|------|--------|------|-------------|-----|
| `nvs` | NVS | 0x9000 | 24 KB | (NVS API) | Config, PIN hash |
| `phy_init` | PHY | 0xF000 | 4 KB | - | RF calibration |
| `factory` | App | 0x10000 | 3 MB | - | Firmware binary |
| `storage` | SPIFFS | 0x310000 | 1 MB | `/spiffs` | General file storage |
| `modules` | FAT | 0x410000 | 3.75 MB | `/modules` | Extra module packages |

SD card (if inserted) is mounted at `/sdcard` and used by badusb (payloads),
wiki_eve (BFI capture), blerp (scan log), passive_keystroke (WAV files),
sonar_snoop (echo log).

### Wi-Fi / web server

`hotspot.cpp` starts a SoftAP with SSID and password derived from the device
MAC (unique per device). Contains a `static bool s_wifi_init` guard so calling
`hotspot_start()` a second time (e.g. after the espnow_c2 module stops) does
not double-init the Wi-Fi stack.

`webserver.cpp` serves a PIN-gated web UI at port 80. Cookie-based session auth.
Endpoints include: GET `/` (dashboard), POST `/api/config` (save settings),
POST `/api/upload` (module upload), GET `/api/status` (JSON status).

### BLE stack

All BLE modules use the NimBLE stack included in the ESP-IDF `bt` component.
A singleton initialiser `ble_nimble_ensure_started()` in `mod_common.cpp`
handles the `nimble_port_init()` / `ble_hs_cfg.sync_cb` / `nimble_port_freertos_init()`
sequence with a sync semaphore, ensuring NimBLE is only started once even if
multiple BLE modules are active.

### Security model

| Aspect | Implementation |
|--------|---------------|
| PIN storage | SHA-256 hash stored in NVS, plaintext never persisted |
| PIN comparison | Constant-time `ct_memcmp()` to prevent timing side-channel |
| Default Wi-Fi password | Derived from BT MAC (`ctOS-XXYYZZ!`), unique per device |
| Default SSID | Derived from AP MAC (`ctOS-XXYYZZ`) |
| Web UI upload | PIN-gated; modules stored on SD/FAT, not reflashed |

First boot: change your PIN and Wi-Fi password via the web UI at `192.168.4.1`.

---

## QA Pipeline

Run all three agents without a device:

```bash
make qa
# Runs: agent_correctness -> agent_security -> agent_boot_qa
# Output: host/tests/qa_report.md
```

| Agent | What it checks |
|-------|---------------|
| Correctness | cppcheck + Python static rules (unsafe strcpy, strtok thread-safety, double-free...) |
| Security | flawfinder + 11 project-specific rules (timing side-channels, credential hygiene, unchecked returns...) |
| Boot Tests | 24 Unity tests compiled for host with gcc, no device needed |

### Run boot tests only

```bash
make test
# Builds + runs host/tests/host/ with gcc
```

Latest QA result: [`host/tests/qa_report.md`](host/tests/qa_report.md)

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Run `make qa` before opening a PR.

---

## License

MIT - see `LICENSE`.
