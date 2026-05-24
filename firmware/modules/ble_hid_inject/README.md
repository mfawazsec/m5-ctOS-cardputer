# BLE HID Wireless Keyboard Injection

Advertises as a BLE HID keyboard. Target device (Android/iOS/macOS) pairs to the Cardputer as a Bluetooth keyboard. Physical Cardputer keystrokes are forwarded over BLE to the connected host.

**Range:** ~10m line of sight.

**Payload library:** Shared DuckyScript `.ducky` files from `/sdcard/payloads/` (same format as BadUSB module).

**Target discovery:** Scans nearby BLE devices before advertising; shows list of candidate targets by name/MAC.

**Note:** iOS requires PIN pairing confirmation; Android behavior varies by version.

**Research basis:** BLEDuck, BLE HID Profile specification.
