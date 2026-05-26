#include "mod_common.h"

void mod_show_log_view(const char *module_id, const char *title)
{
    int scroll = 0;
    mod_drain_keys();

    TickType_t last_draw = 0;

    while (true) {
        M5.update();
        CardputerKb.update();

        TickType_t now = xTaskGetTickCount();
        if ((now - last_draw) >= pdMS_TO_TICKS(250)) {
            int count   = module_log_count(module_id);
            int visible = (M5.Display.height() - 28) / MOD_LH;

            M5.Display.fillScreen(TFT_BLACK);
            M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
            M5.Display.setCursor(0, 0);
            M5.Display.printf("LOGS: %s", title);

            if (count == 0) {
                M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
                M5.Display.setCursor(0, 14);
                M5.Display.print("(no logs yet)");
            }

            for (int i = 0; i < visible; i++) {
                int idx = scroll + i;
                if (idx >= count) break;
                char line[MOD_LOG_LINE_LEN];
                if (!module_log_get(module_id, idx, line, sizeof(line))) break;
                M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
                M5.Display.setCursor(0, 14 + i * MOD_LH);
                M5.Display.printf("%.26s", line);
            }

            M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
            M5.Display.setCursor(0, M5.Display.height() - 10);
            M5.Display.print("[,/.] scroll  [L/`] close");

            last_draw = now;
        }

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        auto kb  = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];

        if (key == '`' || key == 27 || key == 'l' || key == 'L') return;
        if ((key == '.' || key == '/') && scroll + (M5.Display.height() - 28) / MOD_LH < module_log_count(module_id)) scroll++;
        if ((key == ',' || key == ';') && scroll > 0) scroll--;

        last_draw = 0;
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

// ---------------------------------------------------------------------------
// Shared NimBLE initialisation (called once, guarded by static flag)
// ---------------------------------------------------------------------------
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "esp_bt.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static volatile bool s_ble_synced = false;
static bool          s_ble_hw_ok  = false;

static void on_ble_sync(void) { s_ble_synced = true; }
static void ble_host_task(void *) { nimble_port_run(); nimble_port_freertos_deinit(); }

bool ble_nimble_hw_ok(void) { return s_ble_hw_ok; }

void ble_nimble_ensure_started(void)
{
    static bool s_started = false;
    if (s_started) {
        if (!s_ble_hw_ok) {
            ESP_LOGE("ble", "BLE unavailable — WiFi AP + BLE requires PSRAM");
            return;
        }
        for (int i = 0; i < 500 && !s_ble_synced; i++) vTaskDelay(pdMS_TO_TICKS(10));
        if (!s_ble_synced) ESP_LOGE("ble", "NimBLE sync timeout (already started)");
        return;
    }
    s_started = true;

    // Pre-flight: if the DMA-capable DRAM pool is too small, calling nimble_port_init()
    // will partially allocate from it before failing, permanently leaking DMA memory and
    // breaking any subsequent I2S peripheral use (sonar_snoop, passive_keystroke).
    // Skip init entirely to preserve the DMA pool.
    size_t dma_free = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    if (dma_free < 16384) {
        ESP_LOGE("ble", "BLE unavailable — DMA pool too small (%u B). WiFi AP + BLE requires PSRAM",
                 (unsigned)dma_free);
        s_ble_hw_ok = false;
        return;
    }

    nimble_port_init();

    // If BT controller failed to start despite the preflight (e.g. general heap
    // exhausted), don't spawn ble_host_task — calling nimble_port_run() on an
    // uninitialised stack causes a LoadProhibited crash.
    if (esp_bt_controller_get_status() < ESP_BT_CONTROLLER_STATUS_ENABLED) {
        ESP_LOGE("ble", "BLE unavailable — BT controller init failed. WiFi AP + BLE requires PSRAM");
        s_ble_hw_ok = false;
        return;
    }

    s_ble_hw_ok = true;
    ble_hs_cfg.sync_cb = on_ble_sync;
    nimble_port_freertos_init(ble_host_task);
    for (int i = 0; i < 500 && !s_ble_synced; i++) vTaskDelay(pdMS_TO_TICKS(10));
    if (!s_ble_synced) ESP_LOGE("ble", "NimBLE sync timeout — BT hardware not ready");
}
