#include "module_api.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>

static const char *TAG = "wiki_eve";

static const ctos_api_t *s_api = nullptr;

// ------------------------------------------------------------------
// BFI capture configuration
// VHT Compressed Beamforming Report frames (802.11ac):
//   Frame Control: Type=0x00 (Management), Subtype=0xD (Action)
//   Action Category: 21 (0x15) VHT, Action Code: 0 (Compressed Beamforming)
// ------------------------------------------------------------------
#define BFI_PSRAM_BUFFER_SIZE   (64 * 1024)   // 64 KB before flush
#define BFI_SD_PATH             "/sdcard/bfi_capture.bin"

static uint8_t *s_psram_buf   = nullptr;
static size_t   s_psram_fill  = 0;
static FILE    *s_bin_file    = nullptr;
static uint32_t s_frame_count = 0;

// ------------------------------------------------------------------
// Helper: write 4-byte little-endian length prefix then data
// ------------------------------------------------------------------
static void flush_psram_to_sd(void)
{
    if (!s_bin_file || s_psram_fill == 0) return;
    size_t written = fwrite(s_psram_buf, 1, s_psram_fill, s_bin_file);
    if (written != s_psram_fill) {
        ESP_LOGW(TAG, "SD write short: %u of %u bytes", (unsigned)written, (unsigned)s_psram_fill);
    }
    fflush(s_bin_file);
    ESP_LOGI(TAG, "Flushed %u bytes to SD", (unsigned)s_psram_fill);
    s_psram_fill = 0;
}

// ------------------------------------------------------------------
// 802.11 frame inspection helpers
// VHT Compressed Beamforming Action frame layout (after radiotap):
//   [0..1]  Frame Control (LE): Type bits [3:2]=00 (Mgmt), Subtype [7:4]=0xD (Action)
//   [2..3]  Duration
//   [4..9]  DA
//   [10..15] SA
//   [16..21] BSSID
//   [22..23] Seq Ctrl
//   [24]    Action Category = 0x15 (VHT)
//   [25]    Action Code     = 0x00 (Compressed Beamforming)
// ------------------------------------------------------------------
#define FC_TYPE_MASK    0x0C
#define FC_TYPE_MGMT    0x00
#define FC_SUBTYPE_MASK 0xF0
#define FC_SUBTYPE_ACTION 0xD0

static bool is_vht_bf_frame(const uint8_t *payload, uint16_t len)
{
    if (len < 26) return false;
    uint8_t fc0 = payload[0]; // first byte of Frame Control
    if ((fc0 & FC_TYPE_MASK) != FC_TYPE_MGMT) return false;
    if ((fc0 & FC_SUBTYPE_MASK) != FC_SUBTYPE_ACTION) return false;
    // Check Action Category 0x15 (VHT) and Action Code 0x00 (Compressed Beamforming)
    if (payload[24] != 0x15) return false;
    if (payload[25] != 0x00) return false;
    return true;
}

// ------------------------------------------------------------------
// Promiscuous RX callback — management-frame filter already applied
// ------------------------------------------------------------------
static void promisc_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (type != WIFI_PKT_MGMT) return;

    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    const uint8_t *payload = pkt->payload;
    uint16_t len = pkt->rx_ctrl.sig_len;

    if (!is_vht_bf_frame(payload, len)) return;

    s_frame_count++;
    ESP_LOGD(TAG, "BFI frame #%lu len=%u", (unsigned long)s_frame_count, len);

    // Store to PSRAM buffer: [4-byte LE frame len][frame bytes]
    if (!s_psram_buf) return;
    size_t needed = 4 + len;
    if (s_psram_fill + needed > BFI_PSRAM_BUFFER_SIZE) {
        flush_psram_to_sd();
    }
    if (s_psram_fill + needed <= BFI_PSRAM_BUFFER_SIZE) {
        // Write length prefix
        s_psram_buf[s_psram_fill++] = (uint8_t)(len & 0xFF);
        s_psram_buf[s_psram_fill++] = (uint8_t)((len >> 8) & 0xFF);
        s_psram_buf[s_psram_fill++] = 0;
        s_psram_buf[s_psram_fill++] = 0;
        memcpy(s_psram_buf + s_psram_fill, payload, len);
        s_psram_fill += len;
    }
}

static void module_task(void *arg)
{
    // Allocate PSRAM capture buffer via API
    s_psram_buf = (uint8_t *)s_api->psram_alloc(BFI_PSRAM_BUFFER_SIZE);
    if (!s_psram_buf) {
        s_api->log(TAG, "PSRAM alloc failed — falling back to heap");
        s_psram_buf = (uint8_t *)malloc(BFI_PSRAM_BUFFER_SIZE);
    }
    if (!s_psram_buf) {
        s_api->log(TAG, "Fatal: no buffer available");
        vTaskDelete(nullptr);
        return;
    }

    // Open SD output file
    s_bin_file = fopen(BFI_SD_PATH, "ab");
    if (!s_bin_file) {
        s_api->log(TAG, "Warning: cannot open " BFI_SD_PATH);
    }

    // Set promiscuous filter to management frames only
    wifi_promiscuous_filter_t filt = {};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&filt);

    // Register RX callback and enable promiscuous mode
    esp_wifi_set_promiscuous_rx_cb(promisc_rx_cb);
    esp_wifi_set_promiscuous(true);

    s_api->log(TAG, "WiKI-Eve BFI capture active");

    char disp[64];
    uint32_t last_display = 0;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (now - last_display >= 2000) {
            last_display = now;
            snprintf(disp, sizeof(disp),
                     "WiKI-Eve\nBFI frames: %lu\nBuf: %u KB",
                     (unsigned long)s_frame_count,
                     (unsigned)(s_psram_fill / 1024));
            s_api->display_print(disp);
        }

        // Periodic flush if buffer is getting full (>48KB)
        if (s_psram_fill > (BFI_PSRAM_BUFFER_SIZE * 3 / 4)) {
            flush_psram_to_sd();
        }
    }

    // Cleanup (unreachable)
    flush_psram_to_sd();
    esp_wifi_set_promiscuous(false);
    if (s_bin_file) fclose(s_bin_file);
    vTaskDelete(nullptr);
}

extern "C" esp_err_t module_main(const ctos_api_t *api)
{
    s_api = api;
    api->log(TAG, "Module started");
    xTaskCreate(module_task, TAG, 8192, nullptr, 5, nullptr);
    return ESP_OK;
}
