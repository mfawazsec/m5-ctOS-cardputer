#include "module_api.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>
#include <stdint.h>

static const char *TAG = "nuit_inject";
static const ctos_api_t *s_api = nullptr;

#define SAMPLE_RATE   44100
#define CARRIER_HZ    18500.0f   // SSB-AM upper sideband carrier
#define PI            3.14159265358979f

static i2s_chan_handle_t s_tx_chan;

// Pre-defined command PCM: 0.5-second silence placeholder.
// Production: load from /sdcard/commands/<name>.raw (16kHz mono PCM, then modulate).
static const char *s_commands[] = {
    "turn off wifi",
    "set alarm 7am",
    "open browser",
    "call home",
    "read messages",
};
static const int s_cmd_count = sizeof(s_commands) / sizeof(s_commands[0]);

// SSB-AM modulate a baseband tone into a 44.1kHz PCM buffer.
// For demo: generates a 400Hz modulating tone (simulate voice).
// Real: load baseband audio from SD card.
static void synthesize_ssb_burst(int16_t *out, size_t num_samples,
                                  float modulating_hz)
{
    float carrier    = CARRIER_HZ;
    float usb_freq   = carrier + modulating_hz;

    for (size_t i = 0; i < num_samples; i++) {
        float t   = (float)i / SAMPLE_RATE;
        // USB-AM: cos(2π·(fc+fm)·t) — direct tone for demo
        float s   = cosf(2.0f * PI * usb_freq * t);
        out[i]    = (int16_t)(s * 20000.0f);  // ~60% of int16 max
    }
}

static void inject_command(int cmd_idx)
{
    const size_t burst_samples = SAMPLE_RATE / 2;  // 0.5 second burst
    int16_t *buf = (int16_t *)s_api->psram_alloc(burst_samples * sizeof(int16_t));
    if (!buf) {
        s_api->log(TAG, "PSRAM alloc failed");
        return;
    }

    // Modulating frequency: 400Hz base + 200Hz per command slot to differentiate
    float mod_hz = 400.0f + 200.0f * cmd_idx;
    synthesize_ssb_burst(buf, burst_samples, mod_hz);

    char msg[64];
    snprintf(msg, sizeof(msg), "Injecting: %s", s_commands[cmd_idx]);
    s_api->display_print(TAG, msg);
    s_api->log(TAG, msg);

    size_t written;
    i2s_channel_write(s_tx_chan, buf, burst_samples * sizeof(int16_t),
                      &written, pdMS_TO_TICKS(2000));

    s_api->psram_free(buf);
    s_api->display_print(TAG, "Injection complete.");
}

static void nuit_task(void *arg)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                     I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_34,   // NS4168 BCK
            .ws   = GPIO_NUM_33,   // NS4168 WS
            .dout = GPIO_NUM_35,   // NS4168 DIN
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {},
        },
    };
    i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    i2s_channel_enable(s_tx_chan);

    // Show command menu
    char menu[256];
    int off = 0;
    off += snprintf(menu + off, sizeof(menu) - off, "NUIT Commands:\n");
    for (int i = 0; i < s_cmd_count; i++)
        off += snprintf(menu + off, sizeof(menu) - off,
                        "[%d] %s\n", i + 1, s_commands[i]);
    s_api->display_print(TAG, menu);
    s_api->log(TAG, "Ready. Type 1-5 to inject.");

    // In production: read keyboard via IPC or queue; here we inject cmd 0 as demo
    vTaskDelay(pdMS_TO_TICKS(2000));
    inject_command(0);

    while (true) vTaskDelay(pdMS_TO_TICKS(5000));
}

extern "C" esp_err_t module_main(const ctos_api_t *api)
{
    s_api = api;
    api->log(TAG, "NUIT inject starting");
    xTaskCreate(nuit_task, TAG, 8192, nullptr, 5, nullptr);
    return ESP_OK;
}
