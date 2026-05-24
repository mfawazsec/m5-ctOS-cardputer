#include "module_api.h"
#include "registry.h"
#include "loader.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "M5Unified.h"
#include "cardputer_keyboard.h"
#include "ui/mod_common.h"
#include <cstdio>
#include <cstring>
#include <cmath>

static const char *TAG = "passive_wifi_csi";
static const char *ID  = "passive_wifi_csi";
static const ctos_api_t *s_api = nullptr;

static QueueHandle_t s_csi_queue = nullptr;
static FILE         *s_csv_file  = nullptr;

#define CSI_SUBCARRIER_COUNT 52
#define CSI_QUEUE_DEPTH       8

typedef struct {
    int8_t   amplitudes[CSI_SUBCARRIER_COUNT];
    uint32_t timestamp_ms;
} csi_frame_t;

static volatile uint32_t s_frame_count = 0;
static volatile bool     s_capturing   = true;
// Bar graph for display: 20 chars
static char s_bar_buf[22] = "                    ";

static void csi_rx_callback(void *ctx, wifi_csi_info_t *data)
{
    if (!data || !data->buf || !s_capturing) return;
    csi_frame_t frame = {};
    frame.timestamp_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    int len = (data->len < (CSI_SUBCARRIER_COUNT * 2)) ? data->len : (CSI_SUBCARRIER_COUNT * 2);
    for (int i = 0; i < len / 2 && i < CSI_SUBCARRIER_COUNT; i++) {
        int8_t im = data->buf[2*i], re = data->buf[2*i+1];
        float amp = sqrtf((float)(re*re) + (float)(im*im));
        frame.amplitudes[i] = (int8_t)(amp > 127.0f ? 127 : (int8_t)amp);
    }
    xQueueSendFromISR(s_csi_queue, &frame, nullptr);
}

static void render_bar_graph(const int8_t *amps, char *out)
{
    const int bars = 20, step = CSI_SUBCARRIER_COUNT / bars;
    const char chars[] = " _-=#";
    for (int b = 0; b < bars; b++) {
        int sum = 0;
        for (int j = 0; j < step; j++) sum += amps[b*step+j];
        int avg = sum / step;
        int idx = avg * 4 / 127;
        if (idx > 4) idx = 4;
        out[b] = chars[idx];
    }
    out[bars] = '\0';
}

static void csi_task(void *arg)
{
    s_csv_file = fopen("/sdcard/csi_log.csv", "a");
    if (s_csv_file) {
        fseek(s_csv_file, 0, SEEK_END);
        if (ftell(s_csv_file) == 0) {
            fprintf(s_csv_file, "timestamp_ms");
            for (int i = 0; i < CSI_SUBCARRIER_COUNT; i++) fprintf(s_csv_file, ",sc%d", i);
            fprintf(s_csv_file, "\n");
        }
    }

    wifi_csi_config_t cfg = {};
    cfg.lltf_en = cfg.htltf_en = cfg.stbc_htltf2_en = cfg.ltf_merge_en = true;
    esp_wifi_set_csi_config(&cfg);
    esp_wifi_set_csi_rx_cb(csi_rx_callback, nullptr);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_csi(true);

    s_api->log(ID, "CSI collection active");

    csi_frame_t frame;
    while (module_registry_is_running(ID)) {
        if (xQueueReceive(s_csi_queue, &frame, pdMS_TO_TICKS(200)) != pdTRUE) continue;
        s_frame_count++;
        render_bar_graph(frame.amplitudes, s_bar_buf);

        char status[64];
        snprintf(status, sizeof(status), "#%lu [%s]", (unsigned long)s_frame_count, s_bar_buf);
        s_api->display_print(ID, status);

        if (s_csv_file) {
            fprintf(s_csv_file, "%lu", (unsigned long)frame.timestamp_ms);
            for (int i = 0; i < CSI_SUBCARRIER_COUNT; i++) fprintf(s_csv_file, ",%d", frame.amplitudes[i]);
            fprintf(s_csv_file, "\n");
            if (s_frame_count % 50 == 0) fflush(s_csv_file);
        }
    }

    esp_wifi_set_csi(false);
    esp_wifi_set_promiscuous(false);
    if (s_csv_file) { fclose(s_csv_file); s_csv_file = nullptr; }
    s_api->log(ID, "CSI collection stopped");
    module_registry_set_running(ID, false);
    vTaskDelete(nullptr);
}

extern "C" esp_err_t passive_wifi_csi_main(const ctos_api_t *api)
{
    if (module_registry_is_running(ID)) return ESP_OK;
    s_api         = api;
    s_frame_count = 0;
    s_capturing   = true;

    if (!s_csi_queue)
        s_csi_queue = xQueueCreate(CSI_QUEUE_DEPTH, sizeof(csi_frame_t));
    if (!s_csi_queue) { api->log(ID, "Queue alloc failed"); return ESP_ERR_NO_MEM; }

    module_registry_set_running(ID, true);
    if (xTaskCreate(csi_task, TAG, 8192, nullptr, 5, nullptr) != pdPASS) {
        module_registry_set_running(ID, false);
        return ESP_FAIL;
    }
    return ESP_OK;
}

extern "C" void passive_wifi_csi_ui_show(void)
{
    module_loader_start(ID);
    mod_drain_keys();
    bool log_view = false;

    while (true) {
        M5.update();
        CardputerKb.update();

        if (log_view) { mod_show_log_view(ID, "WIFI CSI"); log_view = false; mod_drain_keys(); continue; }

        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        M5.Display.setCursor(0, 0); M5.Display.print("WIFI CSI SENSING");

        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.setCursor(0, 14);
        M5.Display.printf("Status: %s", s_capturing ? "CAPTURING" : "PAUSED");
        M5.Display.setCursor(0, 26);
        M5.Display.printf("Frames: %lu", (unsigned long)s_frame_count);
        M5.Display.setCursor(0, 38);
        M5.Display.printf("CSI:  [%s]", s_bar_buf);
        M5.Display.setCursor(0, 50);
        M5.Display.print("Subcarriers: 52 (20MHz HT)");
        M5.Display.setCursor(0, 62);
        M5.Display.print("Log: /sdcard/csi_log.csv");
        M5.Display.setCursor(0, 74);
        M5.Display.print("Technique: WiFi motion sense");

        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.setCursor(0, M5.Display.height() - 10);
        M5.Display.print("[SPC]toggle [L]log [`]back");

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        auto kb = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];

        if (key == '`' || key == 27) return;
        else if (key == 'l' || key == 'L') log_view = true;
        else if (key == ' ') s_capturing = !s_capturing;
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
