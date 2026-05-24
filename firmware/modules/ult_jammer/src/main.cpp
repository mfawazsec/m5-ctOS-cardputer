#include "module_api.h"
#include "registry.h"
#include "loader.h"
#include "esp_log.h"
#include "esp_random.h"
#include "driver/i2s_std.h"
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
#define CHUNK_SAMPLES  (SAMPLE_RATE / 10)
#define PI             3.14159265358979f

typedef enum { INTENSITY_LOW = 0, INTENSITY_MED, INTENSITY_HIGH } intensity_t;
static volatile intensity_t s_intensity    = INTENSITY_MED;
static volatile bool        s_jammer_on    = true;

static i2s_chan_handle_t s_tx_chan;

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
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = GPIO_NUM_34,
                      .ws = GPIO_NUM_33, .dout = GPIO_NUM_35,
                      .din = I2S_GPIO_UNUSED, .invert_flags = {} },
    };
    i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    i2s_channel_enable(s_tx_chan);

    int16_t *buf = (int16_t *)s_api->psram_alloc(CHUNK_SAMPLES * sizeof(int16_t));
    if (!buf) { s_api->log(ID, "PSRAM alloc failed"); module_registry_set_running(ID, false); vTaskDelete(NULL); return; }

    static const int16_t amps[] = { 8000, 16000, 28000 };
    static const char *level_str[] = { "LOW", "MED", "HIGH" };

    while (module_registry_is_running(ID)) {
        if (!s_jammer_on) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }

        generate_noise_chunk(buf, CHUNK_SAMPLES, amps[s_intensity]);
        size_t written;
        i2s_channel_write(s_tx_chan, buf, CHUNK_SAMPLES * sizeof(int16_t), &written, pdMS_TO_TICKS(500));

        char status[48];
        snprintf(status, sizeof(status), "Jammer: %s | 18-22kHz", level_str[s_intensity]);
        s_api->display_print(ID, status);
    }

    i2s_channel_disable(s_tx_chan);
    i2s_del_channel(s_tx_chan);
    s_api->psram_free(buf);
    s_api->log(ID, "Jammer stopped");
    module_registry_set_running(ID, false);
    vTaskDelete(NULL);
}

extern "C" esp_err_t ult_jammer_main(const ctos_api_t *api)
{
    if (module_registry_is_running(ID)) return ESP_OK;
    s_api      = api;
    s_jammer_on = true;
    module_registry_set_running(ID, true);
    if (xTaskCreate(jammer_task, TAG, 8192, nullptr, 5, nullptr) != pdPASS) {
        module_registry_set_running(ID, false);
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

    while (true) {
        M5.update();
        CardputerKb.update();

        if (log_view) { mod_show_log_view(ID, "ULT JAMMER"); log_view = false; mod_drain_keys(); continue; }

        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
        M5.Display.setCursor(0, 0); M5.Display.print("ULTRASONIC JAMMER");

        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.setCursor(0, 14);
        M5.Display.printf("Status:    %s", s_jammer_on ? "ON  <ACTIVE>" : "OFF");
        M5.Display.setCursor(0, 26);
        M5.Display.printf("Intensity: %s", level_str[s_intensity]);
        M5.Display.setCursor(0, 38);
        M5.Display.print("Freq:      18-22kHz sweep");
        M5.Display.setCursor(0, 50);
        M5.Display.print("Output:    NS4168 I2S amp");
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.setCursor(0, 62);
        M5.Display.print("WARN: HIGH may damage hearing");

        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.setCursor(0, M5.Display.height() - 22);
        M5.Display.print("[1]Low [2]Med [3]High");
        M5.Display.setCursor(0, M5.Display.height() - 10);
        M5.Display.print("[SPC]toggle [L]log [`]back");

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        auto kb = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];

        if (key == '`' || key == 27) return;
        else if (key == 'l' || key == 'L') log_view = true;
        else if (key == '1') s_intensity = INTENSITY_LOW;
        else if (key == '2') s_intensity = INTENSITY_MED;
        else if (key == '3') s_intensity = INTENSITY_HIGH;
        else if (key == ' ') s_jammer_on = !s_jammer_on;
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
