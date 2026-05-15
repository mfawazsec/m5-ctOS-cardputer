#include "module_api.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/i2s_pdm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

static const char *TAG = "sonar_snoop";
static const ctos_api_t *s_api = nullptr;

#define SAMPLE_RATE    44100
#define PING_HZ        20000.0f
#define PING_SAMPLES   (SAMPLE_RATE * 5 / 1000)   // 5ms ping
#define LISTEN_SAMPLES (SAMPLE_RATE * 20 / 1000)  // 20ms listen
#define PI             3.14159265358979f

static i2s_chan_handle_t s_tx_chan;
static i2s_chan_handle_t s_rx_chan;
static FILE             *s_log = nullptr;
static float             s_baseline = 0.0f;

// Simple IIR bandpass coefficients for ~20kHz at 44.1kHz Fs
// Pre-computed 2nd-order butterworth bandpass (18–22kHz)
static float iir_filter(float x, float *z)
{
    // Coefficients computed offline for fs=44100, fc=20000, BW=4000
    static const float b[] = { 0.0929f, 0.0f, -0.0929f };
    static const float a[] = { 1.0f, -1.7539f, 0.8143f };
    float y = b[0]*x + b[1]*z[0] + b[2]*z[1] - a[1]*z[0] - a[2]*z[1];
    z[1] = z[0];
    z[0] = y;
    return y;
}

static float rms_f(const int16_t *buf, size_t n)
{
    double sum = 0;
    for (size_t i = 0; i < n; i++) sum += (double)buf[i] * buf[i];
    return sqrtf((float)(sum / n));
}

static void sonar_task(void *arg)
{
    // TX: NS4168 I2S amplifier
    i2s_chan_config_t tx_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    i2s_new_channel(&tx_cfg, &s_tx_chan, NULL);
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                     I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = GPIO_NUM_34,
                      .ws = GPIO_NUM_33, .dout = GPIO_NUM_35,
                      .din = I2S_GPIO_UNUSED, .invert_flags = {} },
    };
    i2s_channel_init_std_mode(s_tx_chan, &std_cfg);

    // RX: SPM1423 PDM mic
    i2s_chan_config_t rx_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&rx_cfg, NULL, &s_rx_chan);
    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                    I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .clk = GPIO_NUM_41, .din = GPIO_NUM_40,
                      .invert_flags = { .clk_inv = false } },
    };
    i2s_channel_init_pdm_rx_mode(s_rx_chan, &pdm_cfg);

    // Pre-build ping waveform
    int16_t ping_buf[PING_SAMPLES];
    for (int i = 0; i < PING_SAMPLES; i++) {
        float t    = (float)i / SAMPLE_RATE;
        ping_buf[i] = (int16_t)(sinf(2.0f * PI * PING_HZ * t) * 28000.0f);
    }

    s_log = fopen("/sdcard/sonar_echo.bin", "ab");
    if (!s_log) s_api->log(TAG, "WARNING: SD not mounted");

    int16_t echo_buf[LISTEN_SAMPLES];
    float   z[2] = {};
    char    bar[32];

    s_api->display_print(TAG, "SonarSnoop active — 20kHz ping");

    while (true) {
        // PING phase — enable TX, send burst
        i2s_channel_enable(s_tx_chan);
        size_t written;
        i2s_channel_write(s_tx_chan, ping_buf, sizeof(ping_buf),
                          &written, pdMS_TO_TICKS(50));
        i2s_channel_disable(s_tx_chan);

        // LISTEN phase — enable RX, capture echo
        i2s_channel_enable(s_rx_chan);
        size_t bytes_read;
        i2s_channel_read(s_rx_chan, echo_buf, sizeof(echo_buf),
                         &bytes_read, pdMS_TO_TICKS(50));
        i2s_channel_disable(s_rx_chan);

        // Bandpass filter and compute energy
        float filtered[LISTEN_SAMPLES];
        for (int i = 0; i < LISTEN_SAMPLES; i++)
            filtered[i] = iir_filter((float)echo_buf[i], z);
        float energy = rms_f(echo_buf, LISTEN_SAMPLES);

        // Calibrate baseline over first 10 pings
        static int warmup = 0;
        if (warmup < 10) { s_baseline = (s_baseline * warmup + energy) / (warmup + 1); warmup++; }

        float delta = energy - s_baseline;
        if (s_log) { fwrite(echo_buf, 2, LISTEN_SAMPLES, s_log); fflush(s_log); }

        // ASCII bar display
        int bars = (int)(delta / 500.0f);
        if (bars < 0) bars = 0;
        if (bars > 20) bars = 20;
        memset(bar, ' ', sizeof(bar));
        for (int i = 0; i < bars; i++) bar[i] = '|';
        bar[20] = '\0';
        char status[48];
        snprintf(status, sizeof(status), "Echo delta: [%s] %.0f", bar, delta);
        s_api->display_print(TAG, status);

        vTaskDelay(pdMS_TO_TICKS(25));  // ~40Hz cycle
    }
}

extern "C" esp_err_t module_main(const ctos_api_t *api)
{
    s_api = api;
    api->log(TAG, "SonarSnoop starting");
    xTaskCreate(sonar_task, TAG, 8192, nullptr, 5, nullptr);
    return ESP_OK;
}
