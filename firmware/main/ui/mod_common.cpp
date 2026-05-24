#include "mod_common.h"

void mod_show_log_view(const char *module_id, const char *title)
{
    int scroll = 0;
    mod_drain_keys();

    while (true) {
        M5.update();
        CardputerKb.update();

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
            M5.Display.printf("%.27s", line);
        }

        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.setCursor(0, M5.Display.height() - 10);
        M5.Display.print("[,/.] scroll  [L/`] close");

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        auto kb  = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];

        if (key == '`' || key == 27 || key == 'l' || key == 'L') return;
        if ((key == '.' || key == '/') && scroll + visible < count) scroll++;
        if ((key == ',' || key == ';') && scroll > 0) scroll--;

        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

// ---------------------------------------------------------------------------
// Shared NimBLE initialisation (called once, guarded by static flag)
// ---------------------------------------------------------------------------
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "esp_log.h"

static volatile bool s_ble_synced = false;

static void on_ble_sync(void) { s_ble_synced = true; }
static void ble_host_task(void *) { nimble_port_run(); nimble_port_freertos_deinit(); }

void ble_nimble_ensure_started(void)
{
    static bool s_started = false;
    if (s_started) {
        while (!s_ble_synced) vTaskDelay(pdMS_TO_TICKS(10));
        return;
    }
    s_started = true;
    nimble_port_init();
    ble_hs_cfg.sync_cb = on_ble_sync;
    nimble_port_freertos_init(ble_host_task);
    while (!s_ble_synced) vTaskDelay(pdMS_TO_TICKS(10));
}
