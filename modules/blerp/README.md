# blerp — BLERP BLE Re-Pairing Attack (CI)

BLERP implements the Confused Identity (CI) BLE re-pairing attack from the NDSS 2026 paper by Sacchetti and Antonioli. The module scans nearby BLE peripherals, logs discovered devices by name and MAC address, and attempts unauthenticated CI pairing against each target.

## Attack Model

**Confused Identity (CI):** An attacker initiates a new SMP pairing request to a peripheral that has previously bonded with a legitimate device. If the peripheral does not verify that the initiator holds the correct LTK from the prior bond, it may accept the new pairing — effectively granting the attacker a fresh bond without user confirmation.

**PI (Passkey Inference) attack is NOT implemented.** The ESP32 NimBLE stack's LE Secure Connections implementation mitigates PI by default (ECDH key exchange prevents brute-force inference of the 6-digit passkey). CI is the primary implemented vector.

## Workflow

1. Scans for BLE peripherals for 10 seconds using `NimBLEDevice::getScan()`.
2. Logs each discovered device (name, MAC, RSSI) to display and `/sdcard/blerp_log.txt`.
3. For each discovered device: connects via NimBLE client and attempts unauthenticated SMP pairing (Just Works / no-MITM).
4. Logs pairing outcome (PAIRED / rejected) per device.
5. Repeats scan/attack cycle every 30 seconds.

## Notes

- Successful pairing indicates the peripheral is potentially vulnerable to CI attacks.
- iOS and modern Android enforce pairing confirmation dialogs, which limits effectiveness against user-attended devices.
- Unattended peripherals (IoT sensors, smart locks, audio devices) are the primary target class.

## References

- Sacchetti & Antonioli, "BLERP: BLE Re-Pairing Attacks and Defenses", NDSS 2026.
- NimBLE on ESP-IDF (ESP-IDF v5.x BT component).
- Bluetooth Core Specification 5.4, Vol 3, Part H (Security Manager Protocol).
