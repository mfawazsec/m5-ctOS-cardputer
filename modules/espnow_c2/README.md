# ESP-NOW Covert C2 Channel

Implements peer-to-peer ESP-NOW command-and-control between the Cardputer (controller) and a planted ESP32 implant. No router association required — uses raw 802.11 action frames.

**Range:** ~200m line of sight, ~50m through walls.

**Operation:** Set target implant MAC address, compose command on keyboard, send via ESP-NOW. Responses received in real-time with RSSI display.

**Encryption:** AES-128 via ESP-NOW PMK/LMK (configure before deployment).

**Implant firmware:** See `tools/implant/` — minimal ESP32 firmware that receives commands and executes GPIO control, ping/info responses.

**Research basis:** Espressif ESP-NOW protocol documentation, covert channel theory.
