# BLE HID Inject Research Citations

## Implementation References
- BLEDuck — ESP32-S3 BLE HID keyboard implementation
- NimBLE HID Profile in ESP-IDF
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/nimble/

## BLE Specification
- Bluetooth SIG — HID over GATT Profile (HOGP), v1.0
- Bluetooth Core Spec 5.4 — Vol 3, Part H (Security Manager)
- HID Usage Tables 1.3 — Keyboard/Keypad Page 0x07

## Key Implementation Notes
- NimBLE `NimBLEHIDDevice` wraps GATT HID service + battery service
- Report map must be standard boot-keyboard descriptor for compatibility
- iOS requires "just works" or numeric PIN depending on pairing mode
- Android auto-pairs for `BLE_SM_PAIR_AUTHREQ_BOND` with no display

## Threat Model
Wireless HID injection from up to 10m. No physical access to target required beyond
being in BLE range. Useful where USB access is blocked but Bluetooth is enabled.

## Authorization
Authorized penetration testing, CTF, security research only. Test only against
devices you own or have explicit written authorization to test.
