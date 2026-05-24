#include "module_api.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "ir_dazzle";
static const ctos_api_t *s_api = nullptr;

// Cardputer IR LED on GPIO 44 — do not exceed 100mA peak
#define IR_GPIO         GPIO_NUM_44
#define RMT_RESOLUTION  1000000  // 1 MHz resolution = 1µs per tick

typedef enum {
    MODE_CONTINUOUS = 0,  // 38kHz carrier, continuous
    MODE_BURST,           // 10ms on / 5ms off
    MODE_SWEEP,           // 20–56kHz sweep over 2s
} ir_mode_t;

static ir_mode_t        s_mode   = MODE_CONTINUOUS;
static bool             s_active = true;
static rmt_channel_handle_t s_chan;
static rmt_encoder_handle_t s_encoder;

// Build a raw RMT symbol sequence for one carrier period at given Hz
// Returns number of symbols written
static size_t make_carrier_pulse(rmt_symbol_word_t *buf, size_t max_syms,
                                  uint32_t freq_hz)
{
    uint32_t half_period_us = 500000 / freq_hz;  // half period in µs
    if (half_period_us == 0) half_period_us = 1;
    // One full period = [ON half_period, OFF half_period]
    if (max_syms < 1) return 0;
    buf[0].level0    = 1;
    buf[0].duration0 = half_period_us;
    buf[0].level1    = 0;
    buf[0].duration1 = half_period_us;
    return 1;
}

static void transmit_burst_ms(uint32_t freq_hz, uint32_t on_ms, uint32_t off_ms)
{
    // Simple: build enough symbols for on_ms at freq_hz
    uint32_t cycles = (freq_hz * on_ms) / 1000;
    if (cycles > 512) cycles = 512;

    rmt_symbol_word_t *syms = (rmt_symbol_word_t *)s_api->psram_alloc(
        cycles * sizeof(rmt_symbol_word_t));
    if (!syms) return;

    uint32_t half_us = 500000 / freq_hz;
    for (uint32_t i = 0; i < cycles; i++) {
        syms[i].level0    = 1;
        syms[i].duration0 = half_us;
        syms[i].level1    = 0;
        syms[i].duration1 = half_us;
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
        .gpio_num          = IR_GPIO,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = RMT_RESOLUTION,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    rmt_new_tx_channel(&chan_cfg, &s_chan);

    rmt_copy_encoder_config_t enc_cfg = {};
    rmt_new_copy_encoder(&enc_cfg, &s_encoder);

    rmt_enable(s_chan);
    s_api->display_print(TAG, "IR dazzle ready. [1] Continuous [2] Burst [3] Sweep");

    static const char *mode_str[] = { "CONTINUOUS 38kHz", "BURST 10ms/5ms", "SWEEP 20-56kHz" };

    while (s_active) {
        char status[48];
        snprintf(status, sizeof(status), "IR: %s ACTIVE", mode_str[s_mode]);
        s_api->display_print(TAG, status);

        switch (s_mode) {
        case MODE_CONTINUOUS:
            transmit_burst_ms(38000, 100, 0);
            break;

        case MODE_BURST:
            transmit_burst_ms(38000, 10, 5);
            break;

        case MODE_SWEEP: {
            // Sweep 20–56kHz in 4kHz steps, 250ms each → ~9 steps ~2.25s total
            for (uint32_t f = 20000; f <= 56000 && s_active; f += 4000) {
                transmit_burst_ms(f, 250, 0);
            }
            break;
        }
        }
    }

    rmt_disable(s_chan);
    rmt_del_encoder(s_encoder);
    rmt_del_channel(s_chan);
    s_api->display_print(TAG, "IR dazzle STOPPED");
    vTaskDelete(NULL);
}

extern "C" esp_err_t module_main(const ctos_api_t *api)
{
    s_api = api;
    api->log(TAG, "IR dazzle module starting");
    xTaskCreate(ir_task, TAG, 8192, nullptr, 5, nullptr);
    return ESP_OK;
}
