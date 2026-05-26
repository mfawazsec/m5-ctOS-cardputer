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
#include <string.h>
#include <math.h>
#include <stdio.h>

static const char *TAG = "sonar_snoop";
static const char *ID  = "sonar_snoop";
static const ctos_api_t *s_api = nullptr;

#define SAMPLE_RATE     44100
#define PING_HZ         20000.0f
#define PING_SAMPLES    (SAMPLE_RATE * 5 / 1000)
#define LISTEN_SAMPLES  (SAMPLE_RATE * 20 / 1000)
#define PI              3.14159265358979f

static volatile float s_baseline    = 0.0f;
static volatile float s_last_delta  = 0.0f;
static volatile int   s_ping_count  = 0;
static volatile bool  s_recalibrate = false;

static i2s_chan_handle_t s_rx_chan;

static float iir_filter(float x, float *z)
{
    static const float b[] = { 0.0929f, 0.0f, -0.0929f };
    static const float a[] = { 1.0f, -1.7539f, 0.8143f };
    float y = b[0]*x + b[1]*z[0] + b[2]*z[1] - a[1]*z[0] - a[2]*z[1];
    z[1] = z[0]; z[0] = y;
    return y;
}

static float rms_f(const int16_t *buf, size_t n)
{
    double s = 0;
    for (size_t i = 0; i < n; i++) s += (double)buf[i]*buf[i];
    return sqrtf((float)(s / n));
}

static void sonar_task(void *arg)
{
    // RX: PDM mic — CLK=GPIO43, DIN=GPIO46 (corrected for CardputerADV)
    // Use minimal DMA descriptor count (2×64 = 256B vs default 6×240 = 2880B) to
    // leave enough DMA-capable DRAM for M5.Speaker's I2S TX lazy init (needs ~4KB).
    i2s_chan_config_t rx_cfg = {
        .id = I2S_NUM_0, .role = I2S_ROLE_MASTER,
        .dma_desc_num = 2, .dma_frame_num = 64,
        .auto_clear = false, .intr_priority = 0,
    };
    if (i2s_new_channel(&rx_cfg, NULL, &s_rx_chan) != ESP_OK) {
        s_api->log(ID, "I2S_NUM_0 busy — is passive_keystroke running?");
        module_registry_set_running(ID, false);
        vTaskDelete(NULL);
        return;
    }
    i2s_pdm_rx_config_t pdm = {
        .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .clk = GPIO_NUM_43, .din = GPIO_NUM_46, .invert_flags = { .clk_inv = false } },
    };
    esp_err_t pdm_err = i2s_channel_init_pdm_rx_mode(s_rx_chan, &pdm);
    if (pdm_err != ESP_OK) {
        s_api->log(ID, "I2S PDM init failed — DMA alloc error");
        i2s_del_channel(s_rx_chan);
        module_registry_set_running(ID, false);
        vTaskDelete(NULL);
        return;
    }

    // Enable once — per-cycle enable/disable causes repeated DMA reallocation
    // which fails when the DMA pool is fragmented after other peripherals ran.
    esp_err_t en_err = i2s_channel_enable(s_rx_chan);
    if (en_err != ESP_OK) {
        s_api->log(ID, "I2S enable failed — DMA alloc error");
        i2s_del_channel(s_rx_chan);
        module_registry_set_running(ID, false);
        vTaskDelete(NULL);
        return;
    }

    // TX: pre-generate 5ms 20kHz ping for M5.Speaker.playRaw
    static int16_t s_ping_buf[PING_SAMPLES];
    for (int i = 0; i < PING_SAMPLES; i++) {
        float t       = (float)i / SAMPLE_RATE;
        s_ping_buf[i] = (int16_t)(sinf(2.0f * PI * PING_HZ * t) * 28000.0f);
    }

    M5.Speaker.setVolume(255);

    FILE *log_f = fopen("/sdcard/sonar_echo.bin", "ab");
    if (!log_f) s_api->log(ID, "No SD — logging disabled");

    int16_t echo[LISTEN_SAMPLES];
    int16_t drain[PING_SAMPLES];  // discard samples captured during TX
    float   z[2] = {};
    int     warmup = 0;

    while (module_registry_is_running(ID)) {
        if (s_recalibrate) { s_baseline = 0; warmup = 0; s_recalibrate = false; }

        // TX: send 20kHz ping through M5.Speaker (owns I2S_NUM_1)
        M5.Speaker.playRaw(s_ping_buf, PING_SAMPLES, SAMPLE_RATE, false, 1, 0, true);
        for (int i = 0; i < 20 && M5.Speaker.isPlaying(0); i++)
            vTaskDelay(pdMS_TO_TICKS(1));

        // Drain samples captured during TX (direct coupling artefact)
        size_t dbytes;
        i2s_channel_read(s_rx_chan, drain, sizeof(drain), &dbytes, pdMS_TO_TICKS(20));

        // RX: capture echo — channel stays enabled throughout
        size_t rbytes;
        i2s_channel_read(s_rx_chan, echo, sizeof(echo), &rbytes, pdMS_TO_TICKS(50));

        for (int i = 0; i < LISTEN_SAMPLES; i++) iir_filter((float)echo[i], z);
        float energy = rms_f(echo, LISTEN_SAMPLES);

        if (warmup < 10) { s_baseline = (s_baseline * warmup + energy) / (warmup + 1); warmup++; }

        s_last_delta = energy - s_baseline;
        s_ping_count++;

        if (log_f) { fwrite(echo, 2, LISTEN_SAMPLES, log_f); fflush(log_f); }

        static TickType_t s_last_disp = 0;
        TickType_t now = xTaskGetTickCount();
        if ((now - s_last_disp) >= pdMS_TO_TICKS(500)) {
            char status[48];
            int bars = (int)(s_last_delta / 500.0f);
            if (bars < 0) bars = 0;
            if (bars > 18) bars = 18;
            char bar[20]; for (int i=0;i<18;i++) bar[i]=(i<bars)?'|':' '; bar[18]='\0';
            snprintf(status, sizeof(status), "Echo [%s] %.0f", bar, s_last_delta);
            s_api->display_print(ID, status);
            s_last_disp = now;
        }

        vTaskDelay(pdMS_TO_TICKS(25));
    }

    i2s_channel_disable(s_rx_chan);
    i2s_del_channel(s_rx_chan);
    if (log_f) fclose(log_f);
    s_api->log(ID, "Sonar stopped");
    module_registry_set_running(ID, false);
    vTaskDelete(NULL);
}

extern "C" esp_err_t sonar_snoop_main(const ctos_api_t *api)
{
    if (module_registry_is_running(ID)) return ESP_OK;
    s_api         = api;
    s_ping_count  = 0;
    s_last_delta  = 0;
    s_recalibrate = false;
    module_registry_set_running(ID, true);
    if (xTaskCreate(sonar_task, TAG, 8192, nullptr, 5, nullptr) != pdPASS) {
        module_registry_set_running(ID, false);
        ESP_LOGE(TAG, "xTaskCreate failed — free heap: %u B", (unsigned)esp_get_free_heap_size());
        return ESP_FAIL;
    }
    return ESP_OK;
}

extern "C" void sonar_snoop_ui_show(void)
{
    module_loader_start(ID);
    mod_drain_keys();
    bool log_view = false;
    TickType_t last_draw = 0;

    while (true) {
        M5.update();
        CardputerKb.update();

        if (log_view) { mod_show_log_view(ID, "SONAR SNOOP"); log_view = false; mod_drain_keys(); continue; }

        TickType_t now = xTaskGetTickCount();
        if ((now - last_draw) >= pdMS_TO_TICKS(250)) {
            float delta  = s_last_delta;
            int bars     = (int)(delta / 500.0f);
            if (bars < 0) bars = 0;
            if (bars > 20) bars = 20;
            char bar[22]; for (int i=0;i<20;i++) bar[i]=(i<bars)?'#':' '; bar[20]='\0';

            M5.Display.fillScreen(TFT_BLACK);
            M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
            M5.Display.setCursor(0, 0); M5.Display.print("SONAR SNOOP");

            M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            M5.Display.setCursor(0, 14);
            M5.Display.printf("Pings: %-6d (40Hz)", s_ping_count);
            M5.Display.setCursor(0, 26);
            M5.Display.printf("Baseline: %.0f", s_baseline);
            M5.Display.setCursor(0, 38);
            M5.Display.printf("Delta:    %.0f", delta);
            M5.Display.setCursor(0, 50);
            M5.Display.printf("[%s]", bar);
            M5.Display.setCursor(0, 62);
            M5.Display.print("Freq: 20kHz  5ms burst");
            M5.Display.setCursor(0, 74);
            M5.Display.print("Log: sonar_echo.bin");

            M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
            M5.Display.setCursor(0, M5.Display.height() - 10);
            M5.Display.print("[R]recal [L]log [`]back");

            last_draw = now;
        }

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        auto kb = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];

        if (key == '`' || key == 27) return;
        else if (key == 'l' || key == 'L') log_view = true;
        else if (key == 'r' || key == 'R') s_recalibrate = true;
        last_draw = 0;
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
