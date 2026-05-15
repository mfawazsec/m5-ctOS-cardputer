#include "module_api.h"
#include "esp_log.h"
#include "driver/i2s_pdm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

static const char *TAG = "passive_ks";
static const ctos_api_t *s_api = nullptr;

#define SAMPLE_RATE     16000
#define SAMPLES_PER_KS  512
#define ENERGY_THRESH   800   // Tunable; adjust based on mic gain

static i2s_chan_handle_t s_rx_chan;
static uint32_t          s_ks_count = 0;

// Minimal PCM WAV header writer
static void write_wav_header(FILE *f, uint32_t num_samples)
{
    uint32_t data_size   = num_samples * 2;        // 16-bit mono
    uint32_t riff_size   = 36 + data_size;
    uint32_t byte_rate   = SAMPLE_RATE * 2;
    uint16_t block_align = 2;
    uint16_t bits        = 16;
    uint16_t channels    = 1;
    uint16_t audio_fmt   = 1;  // PCM

    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t chunk = 16; fwrite(&chunk, 4, 1, f);
    fwrite(&audio_fmt,   2, 1, f);
    fwrite(&channels,    2, 1, f);
    fwrite(&SAMPLE_RATE, 4, 1, f);
    fwrite(&byte_rate,   4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits,        2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_size,   4, 1, f);
}

static uint32_t rms(const int16_t *buf, size_t n)
{
    uint64_t sum = 0;
    for (size_t i = 0; i < n; i++)
        sum += (int32_t)buf[i] * buf[i];
    return (uint32_t)sqrtf((float)(sum / n));
}

static void save_keystroke(const int16_t *samples)
{
    char path[64];
    snprintf(path, sizeof(path), "/sdcard/keystrokes/key_%05lu.wav", s_ks_count);

    FILE *f = fopen(path, "wb");
    if (!f) return;
    write_wav_header(f, SAMPLES_PER_KS);
    fwrite(samples, 2, SAMPLES_PER_KS, f);
    fclose(f);
}

static void keystroke_task(void *arg)
{
    // Init I2S PDM RX for SPM1423 MEMS mic
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, NULL, &s_rx_chan);

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                    I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk  = GPIO_NUM_41,  // Cardputer mic clock
            .din  = GPIO_NUM_40,  // Cardputer mic data
            .invert_flags = { .clk_inv = false },
        },
    };
    i2s_channel_init_pdm_rx_mode(s_rx_chan, &pdm_cfg);
    i2s_channel_enable(s_rx_chan);

    // Ensure output directory exists
    mkdir("/sdcard/keystrokes", 0755);

    int16_t buf[SAMPLES_PER_KS];
    size_t  bytes_read;
    char    status[64];
    bool    was_above = false;

    s_api->log(TAG, "Recording — listening for keystrokes");

    while (true) {
        i2s_channel_read(s_rx_chan, buf, sizeof(buf), &bytes_read, portMAX_DELAY);

        uint32_t energy = rms(buf, SAMPLES_PER_KS);
        bool above = (energy > ENERGY_THRESH);

        // Rising edge → keystroke event
        if (above && !was_above) {
            save_keystroke(buf);
            s_ks_count++;
            snprintf(status, sizeof(status), "Keystrokes: %lu  energy: %lu",
                     s_ks_count, energy);
            s_api->display_print(TAG, status);
        }
        was_above = above;
    }
}

extern "C" esp_err_t module_main(const ctos_api_t *api)
{
    s_api = api;
    api->log(TAG, "Passive keystroke logger starting");
    xTaskCreate(keystroke_task, TAG, 8192, nullptr, 5, nullptr);
    return ESP_OK;
}
