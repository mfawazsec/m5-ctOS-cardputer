#include "module_api.h"
#include "registry.h"
#include "loader.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "M5Unified.h"
#include "cardputer_keyboard.h"
#include "ui/mod_common.h"
#include <string.h>
#include <math.h>
#include <stdint.h>

static const char *TAG = "nuit_inject";
static const char *ID  = "nuit_inject";
static const ctos_api_t *s_api = nullptr;

#define SAMPLE_RATE   44100
#define CARRIER_HZ    18500.0f
#define PI            3.14159265358979f

static i2s_chan_handle_t s_tx_chan;
static volatile int      s_inject_cmd = -1;  // -1 = none pending
static volatile bool     s_injecting  = false;

static const char *s_commands[] = {
    "turn off wifi",
    "set alarm 7am",
    "open browser",
    "call home",
    "read messages",
};
static const int s_cmd_count = 5;

static void synthesize_ssb_burst(int16_t *out, size_t n, float mod_hz)
{
    float usb = CARRIER_HZ + mod_hz;
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        out[i]  = (int16_t)(cosf(2.0f * PI * usb * t) * 20000.0f);
    }
}

static void do_inject(int idx)
{
    const size_t burst = SAMPLE_RATE / 2;
    int16_t *buf = (int16_t *)s_api->psram_alloc(burst * sizeof(int16_t));
    if (!buf) { s_api->log(ID, "PSRAM alloc failed"); return; }

    float mod_hz = 400.0f + 200.0f * idx;
    synthesize_ssb_burst(buf, burst, mod_hz);

    char msg[64];
    snprintf(msg, sizeof(msg), "Injecting: %s", s_commands[idx]);
    s_api->display_print(ID, msg);

    size_t written;
    i2s_channel_write(s_tx_chan, buf, burst * sizeof(int16_t), &written, pdMS_TO_TICKS(2000));
    s_api->psram_free(buf);
    s_api->display_print(ID, "Injection complete.");
}

static void nuit_task(void *arg)
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

    s_api->log(ID, "NUIT ready. Select command from UI.");

    while (module_registry_is_running(ID)) {
        int cmd = s_inject_cmd;
        if (cmd >= 0 && cmd < s_cmd_count) {
            s_inject_cmd = -1;
            s_injecting  = true;
            do_inject(cmd);
            s_injecting  = false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    i2s_channel_disable(s_tx_chan);
    i2s_del_channel(s_tx_chan);
    s_api->log(ID, "NUIT stopped");
    module_registry_set_running(ID, false);
    vTaskDelete(NULL);
}

extern "C" esp_err_t nuit_inject_main(const ctos_api_t *api)
{
    if (module_registry_is_running(ID)) return ESP_OK;
    s_api       = api;
    s_inject_cmd = -1;
    s_injecting  = false;
    module_registry_set_running(ID, true);
    if (xTaskCreate(nuit_task, TAG, 8192, nullptr, 5, nullptr) != pdPASS) {
        module_registry_set_running(ID, false);
        return ESP_FAIL;
    }
    return ESP_OK;
}

extern "C" void nuit_inject_ui_show(void)
{
    module_loader_start(ID);
    mod_drain_keys();

    bool log_view = false;
    int  cursor   = 0;

    while (true) {
        M5.update();
        CardputerKb.update();

        if (log_view) { mod_show_log_view(ID, "NUIT INJECT"); log_view = false; mod_drain_keys(); continue; }

        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
        M5.Display.setCursor(0, 0);
        M5.Display.printf("NUIT INJECT  %s", s_injecting ? "<FIRING>" : "READY");

        M5.Display.setCursor(0, 12);
        M5.Display.printf("Carrier: %.0f Hz SSB-AM", CARRIER_HZ);

        for (int i = 0; i < s_cmd_count; i++) {
            bool sel = (i == cursor);
            M5.Display.setTextColor(sel ? TFT_BLACK : TFT_WHITE,
                                    sel ? TFT_CYAN  : TFT_BLACK);
            M5.Display.setCursor(0, 24 + i * MOD_LH);
            M5.Display.printf("[%d] %s", i + 1, s_commands[i]);
        }

        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.setCursor(0, M5.Display.height() - 10);
        M5.Display.print("[,/.] [Ent]inject [L]log [`]bk");

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        auto kb = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];

        if (key == '`' || key == 27) return;
        else if (key == 'l' || key == 'L') log_view = true;
        else if ((key == ',' || key == ';') && cursor > 0) cursor--;
        else if ((key == '.' || key == '/') && cursor < s_cmd_count - 1) cursor++;
        else if (key >= '1' && key <= '5') cursor = key - '1';
        else if (key == '\n' || key == '\r') {
            if (!s_injecting) s_inject_cmd = cursor;
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
