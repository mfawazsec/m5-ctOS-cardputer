#include "module_api.h"
#include "registry.h"
#include "loader.h"
#include "esp_log.h"
#include "driver/i2s_pdm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "M5Unified.h"
#include "cardputer_keyboard.h"
#include "ui/mod_common.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>

static const char *TAG = "passive_ks";
static const char *ID  = "passive_keystroke";
static const ctos_api_t *s_api = nullptr;

#define SAMPLE_RATE      16000
#define SAMPLES_PER_KS   512

static volatile uint32_t s_ks_count    = 0;
static volatile uint32_t s_last_energy = 0;
static volatile uint32_t s_threshold   = 800;
static i2s_chan_handle_t s_rx_chan;

static void write_wav_header(FILE *f, uint32_t num_samples)
{
    uint32_t data_size = num_samples * 2;
    uint32_t riff_size = 36 + data_size;
    uint32_t byte_rate = SAMPLE_RATE * 2;
    uint16_t block_align = 2, bits = 16, channels = 1, fmt = 1;
    fwrite("RIFF",    1, 4, f); fwrite(&riff_size, 4, 1, f);
    fwrite("WAVE",    1, 4, f); fwrite("fmt ", 1, 4, f);
    uint32_t chunk = 16;        fwrite(&chunk, 4, 1, f);
    uint32_t sample_rate = SAMPLE_RATE;
    fwrite(&fmt,        2, 1, f); fwrite(&channels,    2, 1, f);
    fwrite(&sample_rate,4, 1, f); fwrite(&byte_rate,   4, 1, f);
    fwrite(&block_align,2, 1, f); fwrite(&bits,         2, 1, f);
    fwrite("data",   1, 4, f); fwrite(&data_size, 4, 1, f);
}

static uint32_t rms_energy(const int16_t *buf, size_t n)
{
    uint64_t sum = 0;
    for (size_t i = 0; i < n; i++) sum += (int32_t)buf[i] * buf[i];
    return (uint32_t)sqrtf((float)(sum / n));
}

static void save_keystroke(const int16_t *samples)
{
    char path[64];
    snprintf(path, sizeof(path), "/sdcard/keystrokes/key_%05lu.wav", (unsigned long)s_ks_count);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    write_wav_header(f, SAMPLES_PER_KS);
    fwrite(samples, 2, SAMPLES_PER_KS, f);
    fclose(f);
}

static void keystroke_task(void *arg)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan_cfg, NULL, &s_rx_chan) != ESP_OK) {
        s_api->log(ID, "I2S_NUM_0 busy — is sonar_snoop running?");
        module_registry_set_running(ID, false);
        vTaskDelete(NULL);
        return;
    }

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .clk = GPIO_NUM_43, .din = GPIO_NUM_46, .invert_flags = { .clk_inv = false } },
    };
    i2s_channel_init_pdm_rx_mode(s_rx_chan, &pdm_cfg);
    i2s_channel_enable(s_rx_chan);

    mkdir("/sdcard/keystrokes", 0755);

    int16_t buf[SAMPLES_PER_KS];
    size_t  bytes_read;
    bool    was_above = false;

    s_api->log(ID, "Listening for keystrokes");

    while (module_registry_is_running(ID)) {
        i2s_channel_read(s_rx_chan, buf, sizeof(buf), &bytes_read, portMAX_DELAY);

        uint32_t energy = rms_energy(buf, SAMPLES_PER_KS);
        s_last_energy   = energy;
        bool above      = (energy > s_threshold);

        if (above && !was_above) {
            save_keystroke(buf);
            s_ks_count++;
            char status[64];
            snprintf(status, sizeof(status), "KS #%lu  energy:%lu", (unsigned long)s_ks_count, (unsigned long)energy);
            s_api->display_print(ID, status);
        }
        was_above = above;
    }

    i2s_channel_disable(s_rx_chan);
    i2s_del_channel(s_rx_chan);
    s_api->log(ID, "Keystroke logger stopped");
    module_registry_set_running(ID, false);
    vTaskDelete(NULL);
}

extern "C" esp_err_t passive_keystroke_main(const ctos_api_t *api)
{
    if (module_registry_is_running(ID)) return ESP_OK;
    s_api       = api;
    s_ks_count  = 0;
    s_threshold = 800;
    module_registry_set_running(ID, true);
    if (xTaskCreate(keystroke_task, TAG, 8192, nullptr, 5, nullptr) != pdPASS) {
        module_registry_set_running(ID, false);
        ESP_LOGE(TAG, "xTaskCreate failed — free heap: %u B", (unsigned)esp_get_free_heap_size());
        return ESP_FAIL;
    }
    return ESP_OK;
}

extern "C" void passive_keystroke_ui_show(void)
{
    module_loader_start(ID);
    mod_drain_keys();
    bool log_view = false;

    while (true) {
        M5.update();
        CardputerKb.update();

        if (log_view) { mod_show_log_view(ID, "PASSIVE KS"); log_view = false; mod_drain_keys(); continue; }

        // Energy bar
        uint32_t e    = s_last_energy;
        uint32_t thr  = s_threshold;
        int bars      = (int)(e * 20 / (thr * 3 + 1));
        if (bars > 20) bars = 20;

        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        M5.Display.setCursor(0, 0); M5.Display.print("PASSIVE KEYSTROKE LOGGER");

        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.setCursor(0, 14);
        M5.Display.printf("Detected: %lu keystrokes", (unsigned long)s_ks_count);
        M5.Display.setCursor(0, 26);
        M5.Display.printf("Threshold: %lu  [+/-]", (unsigned long)thr);

        // Energy bar
        char bar[22];
        for (int i = 0; i < 20; i++) bar[i] = (i < bars) ? '#' : ' ';
        bar[20] = '\0';
        M5.Display.setCursor(0, 38);
        bool hot = (e > thr);
        M5.Display.setTextColor(hot ? TFT_RED : TFT_WHITE, TFT_BLACK);
        M5.Display.printf("Energy:[%s]", bar);
        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.setCursor(0, 50);
        M5.Display.printf("Raw: %lu", (unsigned long)e);
        M5.Display.setCursor(0, 62);
        M5.Display.print("Mic: SPM1423 PDM (CLK=43/DIN=46)");
        M5.Display.setCursor(0, 74);
        M5.Display.print("Save: /sdcard/keystrokes/");

        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.setCursor(0, M5.Display.height() - 10);
        M5.Display.print("[+/-]thr [R]reset [L]log [`]bk");

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        auto kb = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];

        if (key == '`' || key == 27) return;
        else if (key == 'l' || key == 'L') log_view = true;
        else if (key == '+' || key == '=') s_threshold += 100;
        else if (key == '-' && s_threshold > 100) s_threshold -= 100;
        else if (key == 'r' || key == 'R') s_ks_count = 0;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
