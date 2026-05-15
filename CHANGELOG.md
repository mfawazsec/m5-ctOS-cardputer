# Changelog

All notable changes to m5-ctOS are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added
- Initial repository scaffold: CMakeLists.txt, sdkconfig.defaults, partitions.csv, Makefile
- Base OS source tree: main entry point, WiFi hotspot, HTTP web server with PIN auth
- Module system: loader, manifest parser, registry with FreeRTOS task management
- UI: memory view, file browser, module manager, main menu with keyboard shortcuts
- Web UI: dashboard (index.html), module upload (upload.js), PIN gate (pin_auth.js)
- Settings: NVS-backed persistent config (WiFi SSID/pass, PIN hash, brightness, autoload list)
- Tooling: `tools/package_module.py` — packages compiled module into `.ctm` ZIP bundle
- CI: GitHub Actions workflow (lint → build on ESP-IDF v5.3.1 for esp32s3)
- All 12 module stubs: passive_wifi_csi, blerp, wiki_eve, passive_keystroke, nuit_inject,
  ult_jammer, sonar_snoop, badusb, ble_hid_inject, ir_dazzle, gairoscope, espnow_c2

---

## [v2.0.0-alpha.1] — Target milestone

### Goals
- [ ] Base OS bootable on M5Stack Cardputer ADV
- [ ] WiFi AP starts on boot
- [ ] Web UI PIN auth working in phone browser
- [ ] Memory view renders on Cardputer screen
- [ ] File browser operational (SD card)
- [ ] Module registry functional (load/unload via web UI)

---

## [v2.0.0-alpha.2] — Target milestone

### Goals
- [ ] Module loader working end-to-end
- [ ] First `.ctm` bundle installable via web UI upload
- [ ] `tools/package_module.py` producing valid bundles
- [ ] CI lint + build passing on `dev`

---

## [v2.0.0-beta.1] — Target milestone

### Goals
- [ ] All base OS features complete and stable
- [ ] Modules 01 (passive_wifi_csi), 06 (ult_jammer), 08 (badusb) stable on hardware
- [ ] Full CI pipeline green

---

## [v2.0.0] — Target milestone

### Goals
- [ ] All 12 modules passing basic hardware test
- [ ] Documentation complete (CONTRIBUTING.md, all module READMEs)
- [ ] Branch protection on `main` enforced
