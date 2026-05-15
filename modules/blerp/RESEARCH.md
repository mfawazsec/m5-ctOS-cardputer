# RESEARCH — blerp

## Primary Citations

1. **Sacchetti, D. & Antonioli, D. — "BLERP: BLE Re-Pairing Attacks and Defenses"**
   - Venue: Network and Distributed System Security Symposium (NDSS) 2026
   - Relevance: Defines the CI (Confused Identity) and PI (Passkey Inference) attack classes against BLE SMP pairing. CI relies on the peripheral accepting a new bond from an unverified initiator. PI exploits weaknesses in passkey confirmation exchanges under Numeric Comparison.
   - The paper provides proof-of-concept tooling and affected device lists across major vendors.

2. **NimBLE on ESP-IDF**
   - Apache NimBLE BLE stack, ported to ESP-IDF as the `bt` component.
   - URL: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/bluetooth/nimble/index.html
   - Key APIs: `NimBLEDevice::init`, `NimBLEScan`, `NimBLEClient::connect`, `NimBLEClient::secureConnection`
   - Security manager configuration: `NimBLEDevice::setSecurityAuth`, `NimBLEDevice::setSecurityIOCap`

3. **Bluetooth Core Specification 5.4 — Vol 3, Part H: Security Manager Protocol (SMP)**
   - Defines SMP pairing phases: Feature Exchange, Long Term Key (LTK) generation, Authentication.
   - Association models: Just Works, Numeric Comparison, Passkey Entry, OOB.
   - CI attack surface: §2.3 (Pairing Feature Exchange), §2.4.2 (Just Works — no MITM protection).
   - PI attack surface: §2.3.5.6 (Passkey Entry confirmation value), §C.2.2.5 (SC Passkey Entry).
   - URL: https://www.bluetooth.com/specifications/core54-html/

## Key Technical Notes

- **ESP32 NimBLE PI mitigation:** LE Secure Connections (SC) uses ECDH (Curve P-256), making the LTK computationally infeasible to derive from observed Passkey confirmation values. This is why PI is not implemented — the ESP32 BLE stack is not vulnerable as an initiator.
- **CI attack flow:** Connect → send SMP Pairing Request (AuthReq: Bond=1, MITM=0, SC=1) → if peripheral responds with Pairing Response accepting the exchange, proceed to key distribution. A vulnerable peripheral will issue new LTK without validating prior bond identity.
- **Log format (`/sdcard/blerp_log.txt`):** One line per scan cycle header, one line per device discovered, one line per pairing outcome.
- **Affected device classes (per BLERP paper):** Fitness trackers, Bluetooth speakers, smart home hubs, some HID peripherals from major OEMs.
