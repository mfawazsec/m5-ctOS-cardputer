#include "module_api.h"
#include "registry.h"
#include "loader.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "M5Unified.h"
#include "cardputer_keyboard.h"
#include "ui/mod_common.h"
#include <string.h>
#include <math.h>

static const char *TAG = "ult_jammer";
static const char *ID  = "ult_jammer";
static const ctos_api_t *s_api = nullptr;

#define SAMPLE_RATE    44100
#define CHUNK_SAMPLES  (SAMPLE_RATE / 20)   // 50ms of noise per chunk — fits in heap without PSRAM
#define PI             3.14159265358979f

typedef enum { INTENSITY_LOW = 0, INTENSITY_MED, INTENSITY_HIGH } intensity_t;
static volatile intensity_t s_intensity    = INTENSITY_MED;
static volatile bool        s_jammer_on    = true;

static void generate_noise_chunk(int16_t *buf, size_t n, int16_t amp)
{
    memset(buf, 0, n * sizeof(int16_t));
    for (int t = 0; t < 8; t++) {
        float freq  = 18000.0f + (float)(esp_random() % 4001);
        float phase = (float)(esp_random() % 628) / 100.0f;
        for (size_t i = 0; i < n; i++) {
            float ts = (float)i / SAMPLE_RATE;
            buf[i] += (int16_t)(sinf(2.0f * PI * freq * ts + phase) * amp / 8);
        }
    }
}

static void jammer_task(void *arg)
{
    static const int16_t amps[]      = { 8000, 16000, 28000 };
    static const char *level_str[]   = { "LOW", "MED", "HIGH" };

    int16_t *buf = (int16_t *)s_api->psram_alloc(CHUNK_SAMPLES * sizeof(int16_t));
    if (!buf) {
        s_api->log(ID, "PSRAM alloc failed");
        module_registry_set_running(ID, false);
        vTaskDelete(NULL);
        return;
    }

    M5.Speaker.setVolume(255);

    intensity_t cur_intensity = (intensity_t)-1;
    bool cur_on = false;

    while (module_registry_is_running(ID)) {
        bool intensity_changed = (cur_intensity != s_intensity);
        bool on_changed        = (cur_on != s_jammer_on);

        if (s_jammer_on && (intensity_changed || (on_changed && !cur_on))) {
            cur_intensity = s_intensity;
            cur_on = true;
            generate_noise_chunk(buf, CHUNK_SAMPLES, amps[cur_intensity]);
            M5.Speaker.playRaw(buf, CHUNK_SAMPLES, SAMPLE_RATE, false, 0, 0, true);
        } else if (!s_jammer_on && cur_on) {
            M5.Speaker.stop(0);
            cur_on = false;
        }

        static TickType_t s_last_disp = 0;
        TickType_t now = xTaskGetTickCount();
        if ((now - s_last_disp) >= pdMS_TO_TICKS(2000)) {
            char status[48];
            snprintf(status, sizeof(status), "Jammer: %s | 18-22kHz",
                     s_jammer_on ? level_str[s_intensity] : "OFF");
            s_api->display_print(ID, status);
            s_last_disp = now;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }

    M5.Speaker.stop(0);
    s_api->psram_free(buf);
    s_api->log(ID, "Jammer stopped");
    module_registry_set_running(ID, false);
    vTaskDelete(NULL);
}

extern "C" esp_err_t ult_jammer_main(const ctos_api_t *api)
{
    if (module_registry_is_running(ID)) return ESP_OK;
    s_api       = api;
    s_jammer_on = true;
    module_registry_set_running(ID, true);
    if (xTaskCreate(jammer_task, TAG, 8192, nullptr, 5, nullptr) != pdPASS) {
        module_registry_set_running(ID, false);
        ESP_LOGE(TAG, "xTaskCreate failed — free heap: %u B", (unsigned)esp_get_free_heap_size());
        return ESP_FAIL;
    }
    return ESP_OK;
}

extern "C" void ult_jammer_ui_show(void)
{
    module_loader_start(ID);
    mod_drain_keys();

    bool log_view = false;
    static const char *level_str[] = { "LOW (8000)", "MED (16000)", "HIGH (28000)" };
    TickType_t last_draw = 0;

    while (true) {
        M5.update();
        CardputerKb.update();

        if (log_view) { mod_show_log_view(ID, "ULT JAMMER"); log_view = false; mod_drain_keys(); continue; }

        TickType_t now = xTaskGetTickCount();
        if ((now - last_draw) >= pdMS_TO_TICKS(250)) {
            M5.Display.fillScreen(TFT_BLACK);
            M5.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
            M5.Display.setCursor(0, 0); M5.Display.print("ULTRASONIC JAMMER");

            M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            M5.Display.setCursor(0, 14);
            M5.Display.printf("Status:    %s", s_jammer_on ? "ON  <ACTIVE>" : "OFF");
            M5.Display.setCursor(0, 26);
            M5.Display.printf("Intensity: %s", level_str[s_intensity]);
            M5.Display.setCursor(0, 38);
            M5.Display.print("Freq: 18-22kHz sweep");
            M5.Display.setCursor(0, 50);
            M5.Display.print("Output: NS4168 I2S amp");
            M5.Display.setTextColor(TFT_RED, TFT_BLACK);
            M5.Display.setCursor(0, 62);
            M5.Display.print("WARN: HIGH may damage hearing");

            M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
            M5.Display.setCursor(0, M5.Display.height() - 22);
            M5.Display.print("[1]Low [2]Med [3]High");
            M5.Display.setCursor(0, M5.Display.height() - 10);
            M5.Display.print("[SPC]toggle [L]log [`]back");

            last_draw = now;
        }

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        auto kb = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];

        if (key == '`' || key == 27) return;
        else if (key == 'l' || key == 'L') log_view = true;
        else if (key == '1') s_intensity = INTENSITY_LOW;
        else if (key == '2') s_intensity = INTENSITY_MED;
        else if (key == '3') s_intensity = INTENSITY_HIGH;
        else if (key == ' ') s_jammer_on = !s_jammer_on;
        last_draw = 0;
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
