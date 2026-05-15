# BadUSB Research Citations

## Implementation References
- SuperWiFiDuck — ESP32-S3 USB HID emulation over WiFi
  https://github.com/spacehuhn/wifi_ducky
- DuckyScript Specification — Hak5
  https://docs.hak5.org/hak5-usb-rubber-ducky/

## ESP-IDF APIs
- `esp_hid_device.h` — USB HID device class
- USB OTG in device mode (ESP32-S3 native USB)
- `sdkconfig`: `CONFIG_USB_OTG_SUPPORTED=y`

## Payload Examples (SD card, /sdcard/payloads/)
- reverse_shell.ducky — PowerShell reverse shell launcher
- exfil_curl.ducky — credential exfil via curl to attacker server
- lockscreen_bypass.ducky — OS-specific unlock sequence

## Threat Model
Physical access attack: device plugged in via USB-C appears as keyboard to host OS.
Executes keystrokes at machine speed, bypassing software controls that only monitor network.
Cardputer keyboard to write/edit scripts on-device differentiates from passive BadUSB tools.

## Authorization
Authorized red-team engagements and CTF challenges only. Requires physical USB access.
