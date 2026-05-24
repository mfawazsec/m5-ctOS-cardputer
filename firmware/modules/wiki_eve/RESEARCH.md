# WiKI-Eve Research Citations

## Primary Paper
- Hu, J. et al. — "Password-Stealing without Hacking: Wi-Fi Enabled Practical Keystroke Eavesdropping"
  ACM CCS 2023
  DOI: 10.1145/3576915.3623088

## 802.11 Frame Formats
- IEEE 802.11-2020 §9.3.1.21 — VHT Compressed Beamforming frame
- Beamforming Feedback Information (BFI) subfield encoding

## ESP-IDF APIs
- `esp_wifi_set_promiscuous` — monitor mode activation
- `esp_wifi_set_promiscuous_rx_cb` — frame capture callback
- `WIFI_PROMIS_FILTER_MASK_MGMT` — management frame filter

## Offline Inference
- WiKI-Eve Python model (reference implementation): classify keystrokes from BFI data
- CoAtNet architecture for keystroke classification (Harrison et al. 2023 reuse)

## Threat Model
Passive capture of 802.11ac/ax beamforming feedback information frames from a target
device. BFI encodes the physical channel response; keystroke-induced hand motion perturbs
this response in a classifiable way. Capture is fully passive — no frames injected.

## Authorization
Authorized passive monitoring research only. Monitor mode on any channel may capture
frames from third parties. Comply with local RF monitoring laws.
