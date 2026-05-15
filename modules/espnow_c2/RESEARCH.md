# ESP-NOW C2 Research Citations

## Protocol References
- Espressif ESP-NOW documentation: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html
- ESP-NOW uses 802.11 action frames (type=0, subtype=13); visible to passive observers as management frames with no AP association

## Covert Channel Theory
- Lampson, B. — "A Note on the Confinement Problem", CACM 1973 (original covert channel paper)
- 802.11 action frame covert channels: traffic appears as standard WiFi management traffic

## Security Configuration
- PMK (Primary Master Key): 16-byte network-wide key
- LMK (Local Master Key): 16-byte per-peer key for AES-128 CCM encryption
- Do NOT deploy with encryption disabled in operational environments

## Implant Commands
- "ping" → "pong" (connectivity check)
- "info" → chip info string (fingerprint)
- "gpio:<pin>:<0|1>" → GPIO control

## Threat Model
C2 channel for post-exploitation scenarios where internet connectivity or AP association
would be detected. ESP-NOW frames are difficult to attribute without targeted 802.11
frame inspection equipment.

## Authorization
Authorized red-team engagements only. Deploying implants requires physical access and
explicit client authorization. Never deploy on systems without written permission.
