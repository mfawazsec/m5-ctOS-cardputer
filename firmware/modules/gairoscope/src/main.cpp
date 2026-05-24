#include "module_api.h"
#include "registry.h"
#include "loader.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "M5Unified.h"
#include "cardputer_keyboard.h"
#include "ui/mod_common.h"
#include <string.h>
#include <math.h>
#include <stdint.h>

static const char *TAG = "gairoscope";
static const char *ID  = "gairoscope";
static const ctos_api_t *s_api = nullptr;

#define SAMPLE_RATE      44100
#define FREQ_ZERO        19800.0f
#define FREQ_ONE         20200.0f
#define BIT_DURATION_MS  125
#define BIT_SAMPLES      (SAMPLE_RATE * BIT_DURATION_MS / 1000)
#define PI               3.14159265358979f

static volatile bool     s_transmitting = false;
static volatile uint32_t s_bits_sent    = 0;

static void transmit_bit(uint8_t bit)
{
    float freq  = bit ? FREQ_ONE : FREQ_ZERO;
    int16_t *buf = (int16_t *)s_api->psram_alloc(BIT_SAMPLES * sizeof(int16_t));
    if (!buf) return;
    for (int i = 0; i < BIT_SAMPLES; i++) {
        float t = (float)i / SAMPLE_RATE;
        buf[i]  = (int16_t)(sinf(2.0f * PI * freq * t) * 24000.0f);
    }
    M5.Speaker.playRaw(buf, BIT_SAMPLES, SAMPLE_RATE, false, 1, 0, true);
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(BIT_DURATION_MS + 100);
    while (M5.Speaker.isPlaying(0) && (int32_t)(deadline - xTaskGetTickCount()) > 0)
        vTaskDelay(pdMS_TO_TICKS(1));
    s_api->psram_free(buf);
    s_bits_sent++;
}

static void transmit_message(const char *msg)
{
    size_t len = strlen(msg);
    for (int i = 0; i < 8; i++) transmit_bit(i & 1);
    for (int b = 7; b >= 0; b--) transmit_bit(((uint8_t)len >> b) & 1);
    for (size_t i = 0; i < len && module_registry_is_running(ID); i++) {
        for (int b = 7; b >= 0; b--) transmit_bit(((uint8_t)msg[i] >> b) & 1);
        char status[48];
        snprintf(status, sizeof(status), "TX %zu/%zu '%c' bits:%lu", i+1, len, msg[i], (unsigned long)s_bits_sent);
        s_api->display_print(ID, status);
    }
}

static void gairoscope_task(void *arg)
{
    M5.Speaker.setVolume(255);

    while (module_registry_is_running(ID)) {
        if (!s_transmitting) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        s_api->log(ID, "Transmitting GAIROSCOPE message");
        transmit_message("CTOS");
        s_api->display_print(ID, "TX done. ~8 bits/sec");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    M5.Speaker.stop(0);
    s_api->log(ID, "Gairoscope stopped");
    module_registry_set_running(ID, false);
    vTaskDelete(NULL);
}

extern "C" esp_err_t gairoscope_main(const ctos_api_t *api)
{
    if (module_registry_is_running(ID)) return ESP_OK;
    s_api          = api;
    s_transmitting = false;
    s_bits_sent    = 0;
    module_registry_set_running(ID, true);
    if (xTaskCreate(gairoscope_task, TAG, 8192, nullptr, 5, nullptr) != pdPASS) {
        module_registry_set_running(ID, false);
        return ESP_FAIL;
    }
    return ESP_OK;
}

extern "C" void gairoscope_ui_show(void)
{
    module_loader_start(ID);
    mod_drain_keys();
    bool log_view = false;

    while (true) {
        M5.update();
        CardputerKb.update();

        if (log_view) { mod_show_log_view(ID, "GAIROSCOPE"); log_view = false; mod_drain_keys(); continue; }

        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_MAGENTA, TFT_BLACK);
        M5.Display.setCursor(0, 0); M5.Display.print("GAIROSCOPE");

        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.setCursor(0, 14);
        M5.Display.printf("Status: %s", s_transmitting ? "TRANSMITTING" : "IDLE");
        M5.Display.setCursor(0, 26);
        M5.Display.printf("Freq0:  %.0f Hz (bit 0)", FREQ_ZERO);
        M5.Display.setCursor(0, 38);
        M5.Display.printf("Freq1:  %.0f Hz (bit 1)", FREQ_ONE);
        M5.Display.setCursor(0, 50);
        M5.Display.printf("Bits:   %lu sent", (unsigned long)s_bits_sent);
        M5.Display.setCursor(0, 62);
        M5.Display.print("Rate:   ~8 bits/sec FSK");
        M5.Display.setCursor(0, 74);
        M5.Display.print("Target: phone gyroscope");

        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.setCursor(0, M5.Display.height() - 10);
        M5.Display.print("[SPC]toggle [L]log [`]back");

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        auto kb = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];

        if (key == '`' || key == 27) return;
        else if (key == 'l' || key == 'L') log_view = true;
        else if (key == ' ') s_transmitting = !s_transmitting;
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
