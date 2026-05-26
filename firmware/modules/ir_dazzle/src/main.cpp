#include "module_api.h"
#include "registry.h"
#include "loader.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "M5Unified.h"
#include "cardputer_keyboard.h"
#include "ui/mod_common.h"
#include <string.h>

static const char *TAG   = "ir_dazzle";
static const char *ID    = "ir_dazzle";
static const ctos_api_t *s_api = nullptr;

#define IR_GPIO         GPIO_NUM_44
#define RMT_RESOLUTION  1000000

typedef enum { MODE_CONTINUOUS = 0, MODE_BURST, MODE_SWEEP } ir_mode_t;

static volatile ir_mode_t s_mode   = MODE_CONTINUOUS;
static volatile bool      s_active = true;
static rmt_channel_handle_t s_chan;
static rmt_encoder_handle_t s_encoder;

static void transmit_burst_ms(uint32_t freq_hz, uint32_t on_ms, uint32_t off_ms)
{
    uint32_t cycles = (freq_hz * on_ms) / 1000;
    if (cycles > 512) cycles = 512;

    rmt_symbol_word_t *syms = (rmt_symbol_word_t *)s_api->psram_alloc(
        cycles * sizeof(rmt_symbol_word_t));
    if (!syms) return;

    uint32_t half_us = 500000 / freq_hz;
    for (uint32_t i = 0; i < cycles; i++) {
        syms[i].level0 = 1; syms[i].duration0 = half_us;
        syms[i].level1 = 0; syms[i].duration1 = half_us;
    }

    rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
    rmt_transmit(s_chan, s_encoder, syms, cycles * sizeof(rmt_symbol_word_t), &tx_cfg);
    rmt_tx_wait_all_done(s_chan, pdMS_TO_TICKS(on_ms + 50));
    s_api->psram_free(syms);
    if (off_ms > 0) vTaskDelay(pdMS_TO_TICKS(off_ms));
}

static void ir_task(void *arg)
{
    rmt_tx_channel_config_t chan_cfg = {
        .gpio_num = IR_GPIO, .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION, .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    rmt_new_tx_channel(&chan_cfg, &s_chan);
    rmt_copy_encoder_config_t enc_cfg = {};
    rmt_new_copy_encoder(&enc_cfg, &s_encoder);
    rmt_enable(s_chan);

    module_registry_set_running(ID, true);
    s_api->log(ID, "IR dazzle ready");

    static const char *mode_str[] = { "CONTINUOUS", "BURST", "SWEEP" };
    TickType_t last_disp = 0;
    while (s_active && module_registry_is_running(ID)) {
        TickType_t now = xTaskGetTickCount();
        if ((now - last_disp) >= pdMS_TO_TICKS(1000)) {
            char status[48];
            snprintf(status, sizeof(status), "IR %s ACTIVE", mode_str[s_mode]);
            s_api->display_print(ID, status);
            last_disp = now;
        }

        switch (s_mode) {
        case MODE_CONTINUOUS: transmit_burst_ms(38000, 100, 0); break;
        case MODE_BURST:      transmit_burst_ms(38000, 10, 5);  break;
        case MODE_SWEEP:
            for (uint32_t f = 20000; f <= 56000 && s_active; f += 4000)
                transmit_burst_ms(f, 250, 0);
            break;
        }
    }

    rmt_disable(s_chan);
    rmt_del_encoder(s_encoder);
    rmt_del_channel(s_chan);
    s_active = true;  // reset for next start
    s_api->log(ID, "IR dazzle stopped");
    module_registry_set_running(ID, false);
    vTaskDelete(NULL);
}

extern "C" esp_err_t ir_dazzle_main(const ctos_api_t *api)
{
    if (module_registry_is_running(ID)) return ESP_OK;
    s_api   = api;
    s_active = true;
    module_registry_set_running(ID, true);
    if (xTaskCreate(ir_task, TAG, 8192, nullptr, 5, nullptr) != pdPASS) {
        module_registry_set_running(ID, false);
        ESP_LOGE(TAG, "xTaskCreate failed — free heap: %u B", (unsigned)esp_get_free_heap_size());
        return ESP_FAIL;
    }
    return ESP_OK;
}

extern "C" void ir_dazzle_ui_show(void)
{
    module_loader_start(ID);
    mod_drain_keys();

    bool log_view = false;
    static const char *mode_names[] = { "CONTINUOUS 38kHz", "BURST 10ms/5ms", "SWEEP 20-56kHz" };

    while (true) {
        M5.update();
        CardputerKb.update();

        if (log_view) { mod_show_log_view(ID, "IR DAZZLE"); log_view = false; mod_drain_keys(); continue; }

        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.setCursor(0, 0); M5.Display.print("IR DAZZLE");

        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.setCursor(0, 14);
        M5.Display.printf("Mode:   %s", mode_names[s_mode]);
        M5.Display.setCursor(0, 26);
        M5.Display.printf("Status: %s", (s_active && module_registry_is_running(ID)) ? "TRANSMITTING" : "STOPPED");
        M5.Display.setCursor(0, 38);
        M5.Display.print("Target: IR Camera/Sensor");
        M5.Display.setCursor(0, 50);
        M5.Display.print("GPIO:   44 (onboard LED)");

        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.setCursor(0, M5.Display.height() - 22);
        M5.Display.print("[1]Cont [2]Burst [3]Sweep");
        M5.Display.setCursor(0, M5.Display.height() - 10);
        M5.Display.print("[SPC]toggle [L]log [`]back");

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        auto kb = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];

        if (key == '`' || key == 27) return;
        else if (key == 'l' || key == 'L') log_view = true;
        else if (key == '1') s_mode = MODE_CONTINUOUS;
        else if (key == '2') s_mode = MODE_BURST;
        else if (key == '3') s_mode = MODE_SWEEP;
        else if (key == ' ') {
            s_active = !s_active;
            if (s_active) module_loader_start(ID);
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
