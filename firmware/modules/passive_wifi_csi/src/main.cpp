#include "module_api.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>

static const char *TAG = "passive_wifi_csi";

static const ctos_api_t *s_api = nullptr;
static QueueHandle_t s_csi_queue = nullptr;
static FILE *s_csv_file = nullptr;

// Max subcarriers in 20MHz HT mode = 52 (data + pilot)
#define CSI_SUBCARRIER_COUNT 52
#define CSI_QUEUE_DEPTH      8

typedef struct {
    int8_t amplitudes[CSI_SUBCARRIER_COUNT];
    uint32_t timestamp_ms;
} csi_frame_t;

// CSI receive callback — called from WiFi driver task context
static void csi_rx_callback(void *ctx, wifi_csi_info_t *data)
{
    if (!data || !data->buf) return;

    csi_frame_t frame = {};
    frame.timestamp_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    // buf contains pairs of (imaginary, real) int8 per subcarrier
    // Amplitude = sqrt(I^2 + Q^2); clamp to available buffer length
    int len = data->len < (CSI_SUBCARRIER_COUNT * 2) ? data->len : (CSI_SUBCARRIER_COUNT * 2);
    for (int i = 0; i < len / 2 && i < CSI_SUBCARRIER_COUNT; i++) {
        int8_t imag = data->buf[2 * i];
        int8_t real = data->buf[2 * i + 1];
        float amp = sqrtf((float)(real * real) + (float)(imag * imag));
        frame.amplitudes[i] = (int8_t)(amp > 127.0f ? 127 : (int8_t)amp);
    }

    // Non-blocking send; drop frame on queue full to avoid blocking WiFi driver
    xQueueSendFromISR(s_csi_queue, &frame, nullptr);
}

// Render a one-line ASCII amplitude bar graph for display (20 bars max)
static void render_bar_graph(const int8_t *amps, int count, char *out, int out_len)
{
    // Downsample to 20 display bars
    const int bars = 20;
    int step = count / bars;
    int pos = 0;
    for (int b = 0; b < bars && pos + 1 < out_len; b++) {
        int sum = 0;
        for (int j = 0; j < step; j++) {
            sum += amps[b * step + j];
        }
        int avg = sum / step;
        // Map 0–127 amplitude to 5 bar heights: ' ','_','-','=','#'
        const char bar_chars[] = " _-=#";
        int idx = avg * 4 / 127;
        if (idx > 4) idx = 4;
        out[pos++] = bar_chars[idx];
    }
    out[pos] = '\0';
}

static void module_task(void *arg)
{
    // Open CSV log file
    s_csv_file = fopen("/sdcard/csi_log.csv", "a");
    if (s_csv_file) {
        // Write header only if file is empty
        fseek(s_csv_file, 0, SEEK_END);
        if (ftell(s_csv_file) == 0) {
            fprintf(s_csv_file, "timestamp_ms");
            for (int i = 0; i < CSI_SUBCARRIER_COUNT; i++) {
                fprintf(s_csv_file, ",sc%d", i);
            }
            fprintf(s_csv_file, "\n");
        }
    } else {
        s_api->log(TAG, "Failed to open /sdcard/csi_log.csv");
    }

    // Configure CSI collection
    wifi_csi_config_t csi_cfg = {};
    csi_cfg.lltf_en           = true;
    csi_cfg.htltf_en          = true;
    csi_cfg.stbc_htltf2_en    = true;
    csi_cfg.ltf_merge_en      = true;
    csi_cfg.channel_filter_en = false;
    csi_cfg.manu_scale        = false;
    esp_wifi_set_csi_config(&csi_cfg);

    // Register CSI callback
    esp_wifi_set_csi_rx_cb(csi_rx_callback, nullptr);

    // Enable promiscuous mode so we receive all frames
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_csi(true);

    s_api->log(TAG, "CSI collection active");

    uint32_t frame_count = 0;
    char disp_buf[64];
    char bar_buf[32];

    csi_frame_t frame;
    while (true) {
        if (xQueueReceive(s_csi_queue, &frame, pdMS_TO_TICKS(200)) == pdTRUE) {
            frame_count++;

            // Log 52 subcarrier amplitudes to ESP log
            char amp_str[256] = {0};
            int pos = 0;
            for (int i = 0; i < CSI_SUBCARRIER_COUNT && pos < (int)sizeof(amp_str) - 5; i++) {
                pos += snprintf(amp_str + pos, sizeof(amp_str) - pos, "%d ", frame.amplitudes[i]);
            }
            ESP_LOGD(TAG, "CSI[%lu] amps: %s", (unsigned long)frame.timestamp_ms, amp_str);

            // Render bar graph for display
            render_bar_graph(frame.amplitudes, CSI_SUBCARRIER_COUNT, bar_buf, sizeof(bar_buf));
            snprintf(disp_buf, sizeof(disp_buf), "CSI #%lu\n%s", (unsigned long)frame_count, bar_buf);
            s_api->display_print(disp_buf);

            // Write to CSV
            if (s_csv_file) {
                fprintf(s_csv_file, "%lu", (unsigned long)frame.timestamp_ms);
                for (int i = 0; i < CSI_SUBCARRIER_COUNT; i++) {
                    fprintf(s_csv_file, ",%d", frame.amplitudes[i]);
                }
                fprintf(s_csv_file, "\n");
                // Flush periodically every 50 frames
                if (frame_count % 50 == 0) {
                    fflush(s_csv_file);
                }
            }
        }
    }

    // Cleanup (unreachable in normal operation)
    esp_wifi_set_csi(false);
    esp_wifi_set_promiscuous(false);
    if (s_csv_file) fclose(s_csv_file);
    vTaskDelete(nullptr);
}

extern "C" esp_err_t module_main(const ctos_api_t *api)
{
    s_api = api;
    api->log(TAG, "Module started");

    s_csi_queue = xQueueCreate(CSI_QUEUE_DEPTH, sizeof(csi_frame_t));
    if (!s_csi_queue) {
        api->log(TAG, "Failed to create CSI queue");
        return ESP_ERR_NO_MEM;
    }

    xTaskCreate(module_task, TAG, 8192, nullptr, 5, nullptr);
    return ESP_OK;
}
