# wiki_eve — WiKI-Eve BFI Keystroke Inference

WiKI-Eve captures 802.11ac/ax VHT Compressed Beamforming Report (BFI) management frames in WiFi monitor mode. These frames contain per-subcarrier beamforming feedback information (BFI) that encodes the instantaneous channel state between a transmitter and its beamforming partner.

## How it works

1. WiFi radio enters promiscuous mode with `WIFI_PROMIS_FILTER_MASK_MGMT` to receive only management frames, reducing CPU load.
2. The RX callback inspects Frame Control bytes for Action frames (Type=0, Subtype=0xD) with Action Category 0x15 (VHT) and Action Code 0x00 (Compressed Beamforming) — the BFI frame signature.
3. Captured frames are accumulated in a 64 KB PSRAM ring buffer (allocated via `api->psram_alloc`) to avoid SD write latency during capture.
4. When the buffer exceeds 64 KB (or 48 KB threshold for partial flushes), frames are bulk-written to `/sdcard/bfi_capture.bin`.
5. Frame count is displayed live via `api->display_print`.

## Binary format

`/sdcard/bfi_capture.bin`: sequence of `[4-byte LE frame length][raw 802.11 frame bytes]` records, no additional header. Process offline with the WiKI-Eve Python inference pipeline.

## Offline analysis

Transfer `bfi_capture.bin` to MacBook. Run the WiKI-Eve inference model (Python/PyTorch) to infer keystroke sequences from BFI amplitude deltas. The model was trained on common keyboard layouts and can achieve >85% top-1 accuracy on numeric digit sequences per the original paper.

## Limitations

- Requires 802.11ac (or ax) network with beamforming enabled; legacy 802.11n does not emit VHT BFI frames.
- Target must be actively transmitting (e.g., typing in a web form over WiFi).
- Best results with target within ~2m of Cardputer.

## References

- Hu et al., "Password-Stealing without Hacking: Wi-Fi Enabled Practical Keystroke Eavesdropping", ACM CCS 2023.
- ESP-IDF promiscuous mode API.
- IEEE 802.11-2020 §9.3.1.21 (VHT Compressed Beamforming frame format).
