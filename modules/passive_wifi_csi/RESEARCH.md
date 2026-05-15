# RESEARCH — passive_wifi_csi

## Primary Citations

1. **Furrtek — ESL (Electronic Shelf Label) Reverse Engineering**
   - URL: https://github.com/furrtek/
   - Relevance: Passive CSI observation in dense 2.4 GHz / 802.11 environments. Demonstrates promiscuous frame capture workflow on ESP32-class hardware.

2. **RuView by ruvnet**
   - URL: https://github.com/ruvnet/ruview
   - Relevance: Real-time CSI motion detection and visualization pipeline. Provides the reference inference model for amplitude-delta threshold-based motion classification on top of ESP-IDF CSI frames.

3. **ESP-IDF WiFi CSI API**
   - Espressif official documentation: `esp_wifi_set_csi()`, `esp_wifi_set_csi_rx_cb()`, `wifi_csi_config_t`, `wifi_csi_info_t`
   - URL: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/network/esp_wifi.html

4. **802.11 Channel State Information — Background**
   - IEEE 802.11-2020, §19 (OFDM PHY layer), §21 (HE PHY layer)
   - CSI encodes per-subcarrier complex channel response H(f); amplitude |H(f)| varies with multipath changes caused by moving objects.
   - HT 20 MHz mode: 52 data + pilot subcarriers (56 total, 4 null).

## Key Technical Notes

- ESP32-S3 reports raw I/Q int8 pairs per subcarrier in `wifi_csi_info_t.buf`.
- `lltf_en` (L-LTF): legacy long training field; most reliable across all frame types.
- `htltf_en` (HT-LTF): HT-mode preamble; higher subcarrier resolution for motion sensing.
- Delta-amplitude threshold for motion: empirically ~8–12 amplitude units per subcarrier, with at least 10 of 52 subcarriers crossing simultaneously.
- CSV logging to SD enables offline processing with Python (numpy/scipy) or feeding into ruvnet's pipeline.
