#include "module_api.h"
#include "registry.h"
#include "loader.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "M5Unified.h"
#include "cardputer_keyboard.h"
#include "ui/mod_common.h"
#include <cstdio>
#include <cstring>

static const char *TAG = "wiki_eve";
static const char *ID  = "wiki_eve";
static const ctos_api_t *s_api = nullptr;

#define BFI_PSRAM_BUFFER_SIZE (8 * 1024)
#define BFI_SD_PATH           "/sdcard/bfi_capture.bin"

static uint8_t  *s_psram_buf   = nullptr;
static size_t    s_psram_fill  = 0;
static FILE     *s_bin_file    = nullptr;
static volatile uint32_t s_frame_count = 0;
static volatile uint8_t  s_channel     = 6;
static volatile bool     s_active      = true;

#define FC_TYPE_MASK      0x0C
#define FC_TYPE_MGMT      0x00
#define FC_SUBTYPE_MASK   0xF0
#define FC_SUBTYPE_ACTION 0xD0

static bool is_vht_bf_frame(const uint8_t *p, uint16_t len)
{
    if (len < 26) return false;
    if ((p[0] & FC_TYPE_MASK)    != FC_TYPE_MGMT)      return false;
    if ((p[0] & FC_SUBTYPE_MASK) != FC_SUBTYPE_ACTION)  return false;
    return (p[24] == 0x15 && p[25] == 0x00);
}

static void flush_to_sd(void)
{
    if (!s_bin_file || s_psram_fill == 0) return;
    fwrite(s_psram_buf, 1, s_psram_fill, s_bin_file);
    fflush(s_bin_file);
    s_api->log(ID, "Flushed BFI buffer to SD");
    s_psram_fill = 0;
}

static void promisc_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (type != WIFI_PKT_MGMT || !s_active) return;
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    const uint8_t *payload = pkt->payload;
    uint16_t len = pkt->rx_ctrl.sig_len;

    if (!is_vht_bf_frame(payload, len)) return;
    s_frame_count++;

    if (!s_psram_buf) return;
    size_t needed = 4 + len;
    if (s_psram_fill + needed > BFI_PSRAM_BUFFER_SIZE) flush_to_sd();
    if (s_psram_fill + needed <= BFI_PSRAM_BUFFER_SIZE) {
        s_psram_buf[s_psram_fill++] = (uint8_t)(len & 0xFF);
        s_psram_buf[s_psram_fill++] = (uint8_t)((len >> 8) & 0xFF);
        s_psram_buf[s_psram_fill++] = 0;
        s_psram_buf[s_psram_fill++] = 0;
        memcpy(s_psram_buf + s_psram_fill, payload, len);
        s_psram_fill += len;
    }
}

static void wiki_task(void *arg)
{
    s_psram_buf = (uint8_t *)s_api->psram_alloc(BFI_PSRAM_BUFFER_SIZE);
    if (!s_psram_buf) {
        s_api->log(ID, "PSRAM alloc failed, using heap");
        s_psram_buf = (uint8_t *)malloc(BFI_PSRAM_BUFFER_SIZE);
    }

    s_bin_file = fopen(BFI_SD_PATH, "ab");
    if (!s_bin_file) s_api->log(ID, "Warning: SD not available");

    wifi_promiscuous_filter_t filt = {};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(promisc_rx_cb);
    esp_wifi_set_promiscuous(true);

    s_api->log(ID, "WiKI-Eve BFI capture active");

    while (module_registry_is_running(ID)) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (s_psram_fill > (BFI_PSRAM_BUFFER_SIZE * 3 / 4)) flush_to_sd();

        char disp[64];
        snprintf(disp, sizeof(disp), "BFI:%lu buf:%uKB",
                 (unsigned long)s_frame_count, (unsigned)(s_psram_fill / 1024));
        s_api->display_print(ID, disp);
    }

    flush_to_sd();
    esp_wifi_set_promiscuous(false);
    if (s_psram_buf) { s_api->psram_free(s_psram_buf); s_psram_buf = nullptr; }
    if (s_bin_file)  { fclose(s_bin_file); s_bin_file = nullptr; }
    s_api->log(ID, "WiKI-Eve stopped");
    module_registry_set_running(ID, false);
    vTaskDelete(nullptr);
}

extern "C" esp_err_t wiki_eve_main(const ctos_api_t *api)
{
    if (module_registry_is_running(ID)) return ESP_OK;
    s_api         = api;
    s_frame_count = 0;
    s_psram_fill  = 0;
    s_active      = true;
    module_registry_set_running(ID, true);
    if (xTaskCreate(wiki_task, TAG, 8192, nullptr, 5, nullptr) != pdPASS) {
        module_registry_set_running(ID, false);
        ESP_LOGE(TAG, "xTaskCreate failed — free heap: %u B", (unsigned)esp_get_free_heap_size());
        return ESP_FAIL;
    }
    return ESP_OK;
}

extern "C" void wiki_eve_ui_show(void)
{
    module_loader_start(ID);
    mod_drain_keys();
    bool log_view = false;

    while (true) {
        M5.update();
        CardputerKb.update();

        if (log_view) { mod_show_log_view(ID, "WiKI-Eve"); log_view = false; mod_drain_keys(); continue; }

        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5.Display.setCursor(0, 0); M5.Display.print("WiKI-Eve BFI CAPTURE");

        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.setCursor(0, 14);
        M5.Display.printf("Status: %s", s_active ? "CAPTURING" : "PAUSED");
        M5.Display.setCursor(0, 26);
        M5.Display.printf("Chan:   %d  [C] change", s_channel);
        M5.Display.setCursor(0, 38);
        M5.Display.printf("BFI:    %lu frames", (unsigned long)s_frame_count);
        M5.Display.setCursor(0, 50);
        M5.Display.printf("PSRAM:  %u / 64 KB", (unsigned)(s_psram_fill / 1024));
        M5.Display.setCursor(0, 62);
        M5.Display.printf("SD:     %s", s_bin_file ? "OPEN" : "UNAVAILABLE");
        M5.Display.setCursor(0, 74);
        M5.Display.print("Target: VHT BF Reports 0x15/0x00");

        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.setCursor(0, M5.Display.height() - 10);
        M5.Display.print("[SPC]toggle [F]flush [L]log [`]bk");

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        auto kb = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];

        if (key == '`' || key == 27) return;
        else if (key == 'l' || key == 'L') log_view = true;
        else if (key == ' ') s_active = !s_active;
        else if (key == 'f' || key == 'F') flush_to_sd();
        else if (key == 'c' || key == 'C') {
            s_channel = (s_channel % 13) + 1;
            esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
