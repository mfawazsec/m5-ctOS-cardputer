# Interactive BadUSB — DuckyScript HID Injector

ESP32-S3 enumerates as a USB HID keyboard to the connected host. Composes and executes DuckyScript payloads via the Cardputer physical keyboard.

**Supported DuckyScript commands:** STRING, DELAY, ENTER, TAB, CTRL, ALT, GUI, SHIFT, DEFAULT_DELAY, REPEAT.

**Payload library:** Load `.ducky` scripts from `/sdcard/payloads/`. Manage via web UI at `/files`.

**Execution log:** Shows each sent keystroke and timing on screen.

**Research basis:** SuperWiFiDuck, Hak5 DuckyScript specification.
