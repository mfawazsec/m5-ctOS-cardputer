# passive_wifi_csi — Passive WiFi Sensing (CSI)

Collects 802.11 Channel State Information (CSI) from the ESP32-S3 WiFi radio in promiscuous mode. CSI exposes per-subcarrier amplitude and phase across 52 OFDM subcarriers (20 MHz HT mode), enabling passive motion detection without any network association.

## How it works

1. WiFi radio enters promiscuous + CSI mode via `esp_wifi_set_promiscuous(true)` and `esp_wifi_set_csi_rx_cb`.
2. Each received frame triggers the CSI callback with a `wifi_csi_info_t` struct containing raw I/Q samples.
3. Amplitude is computed per subcarrier as `sqrt(I² + Q²)`.
4. A scrolling ASCII bar graph of the 52 subcarrier amplitudes is rendered on the Cardputer display via `api->display_print`.
5. Every CSI frame is appended to `/sdcard/csi_log.csv` (timestamp + 52 amplitude columns).

## Motion Detection

Offline: compute per-subcarrier amplitude delta across consecutive frames. A threshold crossing on multiple subcarriers simultaneously indicates human motion in the RF environment. RuView (ruvnet) provides a ready-made inference pipeline for this.

## Research References

- **Furrtek ESL reverse engineering** — passive CSI observation in dense 2.4 GHz environments.
- **RuView by ruvnet** — real-time CSI motion detection pipeline on ESP32 hardware.
- ESP-IDF CSI API documentation (`esp_wifi_set_csi`, `wifi_csi_info_t`).
- 802.11 Channel State Information background: IEEE 802.11-2020 §19 (OFDM PHY).
