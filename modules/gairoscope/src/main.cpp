#include "module_api.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>
#include <stdint.h>

static const char *TAG = "gairoscope";
static const ctos_api_t *s_api = nullptr;

// FSK parameters (per Guri 2022)
// Bit 0: 19800 Hz, Bit 1: 20200 Hz, duration 125ms → 8 bits/sec
#define SAMPLE_RATE   44100
#define FREQ_ZERO     19800.0f
#define FREQ_ONE      20200.0f
#define BIT_DURATION_MS 125
#define BIT_SAMPLES   (SAMPLE_RATE * BIT_DURATION_MS / 1000)  // 5512 samples
#define PI            3.14159265358979f

static i2s_chan_handle_t s_tx_chan;

static void init_i2s_tx(void)
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
}

static void transmit_bit(uint8_t bit)
{
    float freq = bit ? FREQ_ONE : FREQ_ZERO;
    int16_t *buf = (int16_t *)s_api->psram_alloc(BIT_SAMPLES * sizeof(int16_t));
    if (!buf) return;

    for (int i = 0; i < BIT_SAMPLES; i++) {
        float t  = (float)i / SAMPLE_RATE;
        buf[i]   = (int16_t)(sinf(2.0f * PI * freq * t) * 24000.0f);
    }

    size_t written;
    i2s_channel_write(s_tx_chan, buf, BIT_SAMPLES * sizeof(int16_t),
                      &written, pdMS_TO_TICKS(BIT_DURATION_MS + 50));
    s_api->psram_free(buf);
}

static void transmit_byte(uint8_t byte)
{
    for (int b = 7; b >= 0; b--)
        transmit_bit((byte >> b) & 1);
}

static void transmit_message(const char *msg)
{
    size_t len = strlen(msg);
    char status[64];

    // Preamble: 8 alternating bits for sync
    for (int i = 0; i < 8; i++) transmit_bit(i & 1);

    // Length byte
    transmit_byte((uint8_t)len);

    // Payload
    for (size_t i = 0; i < len; i++) {
        transmit_byte((uint8_t)msg[i]);
        snprintf(status, sizeof(status), "TX: %zu/%zu '%c'", i + 1, len, msg[i]);
        s_api->display_print(TAG, status);
    }
}

static void frequency_sweep_mode(void)
{
    // Sweep 18–22kHz to locate target gyroscope resonance empirically
    s_api->display_print(TAG, "Sweeping 18-22kHz for gyro resonance...");
    int16_t *buf = (int16_t *)s_api->psram_alloc(BIT_SAMPLES * sizeof(int16_t));
    if (!buf) return;

    for (float f = 18000.0f; f <= 22000.0f; f += 100.0f) {
        for (int i = 0; i < BIT_SAMPLES; i++) {
            float t = (float)i / SAMPLE_RATE;
            buf[i]  = (int16_t)(sinf(2.0f * PI * f * t) * 24000.0f);
        }
        size_t written;
        i2s_channel_write(s_tx_chan, buf, BIT_SAMPLES * sizeof(int16_t),
                          &written, pdMS_TO_TICKS(BIT_DURATION_MS + 50));

        char status[48];
        snprintf(status, sizeof(status), "Sweep: %.0f Hz", f);
        s_api->display_print(TAG, status);
    }
    s_api->psram_free(buf);
    s_api->display_print(TAG, "Sweep complete. Check receiver HTML page.");
}

static void gairoscope_task(void *arg)
{
    init_i2s_tx();

    s_api->display_print(TAG,
        "GAIROSCOPE\n"
        "[S] Frequency sweep\n"
        "[T] Transmit demo message\n"
        "Open /gairoscope on connected phone.");

    // Demo: transmit a short message
    vTaskDelay(pdMS_TO_TICKS(3000));
    s_api->display_print(TAG, "Transmitting: 'HELLO'");
    transmit_message("HELLO");
    s_api->display_print(TAG, "TX done. Rate: ~8 bits/sec");

    while (true) vTaskDelay(pdMS_TO_TICKS(5000));
}

extern "C" esp_err_t module_main(const ctos_api_t *api)
{
    s_api = api;
    api->log(TAG, "GAIROSCOPE starting");
    xTaskCreate(gairoscope_task, TAG, 8192, nullptr, 5, nullptr);
    return ESP_OK;
}
