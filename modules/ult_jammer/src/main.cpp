#include "module_api.h"
#include "esp_log.h"
#include "esp_random.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "ult_jammer";
static const ctos_api_t *s_api = nullptr;

#define SAMPLE_RATE    44100
#define CHUNK_SAMPLES  (SAMPLE_RATE / 10)   // 100ms window
#define PI             3.14159265358979f

typedef enum { INTENSITY_LOW = 0, INTENSITY_MED, INTENSITY_HIGH } intensity_t;
static intensity_t s_intensity = INTENSITY_MED;
static bool s_running_flag     = true;

static i2s_chan_handle_t s_tx_chan;

// Generate one 100ms band-limited noise chunk (18–22 kHz).
// Approach: sum of sinusoids at random frequencies within the band.
// Each 100ms window uses a fresh random seed to prevent adaptive filtering.
static void generate_noise_chunk(int16_t *buf, size_t n, int16_t amplitude)
{
    memset(buf, 0, n * sizeof(int16_t));
    // 8 random-frequency sinusoids summed, normalized
    for (int tone = 0; tone < 8; tone++) {
        float freq = 18000.0f + (float)(esp_random() % 4001);  // 18–22kHz
        float phase = (float)(esp_random() % 628) / 100.0f;    // random phase
        for (size_t i = 0; i < n; i++) {
            float t = (float)i / SAMPLE_RATE;
            buf[i] += (int16_t)(sinf(2.0f * PI * freq * t + phase)
                                * amplitude / 8);
        }
    }
}

static void jammer_task(void *arg)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                     I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_34,
            .ws   = GPIO_NUM_33,
            .dout = GPIO_NUM_35,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {},
        },
    };
    i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    i2s_channel_enable(s_tx_chan);

    int16_t *buf = (int16_t *)s_api->psram_alloc(CHUNK_SAMPLES * sizeof(int16_t));
    if (!buf) { s_api->log(TAG, "PSRAM alloc failed"); vTaskDelete(NULL); return; }

    s_api->display_print(TAG, "Jammer ACTIVE (18-22kHz)");

    while (s_running_flag) {
        static const int16_t amplitudes[] = { 8000, 16000, 28000 };
        int16_t amp = amplitudes[s_intensity];

        generate_noise_chunk(buf, CHUNK_SAMPLES, amp);

        size_t written;
        i2s_channel_write(s_tx_chan, buf, CHUNK_SAMPLES * sizeof(int16_t),
                          &written, pdMS_TO_TICKS(500));

        static const char *level_str[] = { "LOW", "MED", "HIGH" };
        char status[48];
        snprintf(status, sizeof(status), "Jammer: %s | 18-22kHz sweep",
                 level_str[s_intensity]);
        s_api->display_print(TAG, status);
    }

    i2s_channel_disable(s_tx_chan);
    i2s_del_channel(s_tx_chan);
    s_api->psram_free(buf);
    s_api->display_print(TAG, "Jammer STOPPED");
    vTaskDelete(NULL);
}

extern "C" esp_err_t module_main(const ctos_api_t *api)
{
    s_api = api;
    api->log(TAG, "Ultrasonic jammer starting");
    xTaskCreate(jammer_task, TAG, 8192, nullptr, 5, nullptr);
    return ESP_OK;
}
