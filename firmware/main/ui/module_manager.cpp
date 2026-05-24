#include "module_manager.h"
#include "modules/registry.h"
#include "modules/loader.h"
#include "esp_log.h"
#include "M5Unified.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cardputer_keyboard.h"

#define LINE_HEIGHT 12

static int s_cursor = 0;
static int s_scroll  = 0;

static void render(void)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.print("Module Manager\n");

    int count   = module_registry_count();
    int visible = (M5.Display.height() - 24) / LINE_HEIGHT;

    if (count == 0) {
        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.print("  No modules installed.\n");
        M5.Display.print("  Upload .ctm via web UI.");
    }

    for (int i = 0; i < visible && (s_scroll + i) < count; i++) {
        int idx = s_scroll + i;
        module_info_t info;
        if (module_registry_get(idx, &info) != ESP_OK) continue;
        bool sel = (idx == s_cursor);

        M5.Display.setTextColor(sel ? TFT_BLACK : TFT_WHITE,
                                sel ? TFT_WHITE : TFT_BLACK);
        M5.Display.setCursor(0, 14 + i * LINE_HEIGHT);
        M5.Display.printf("[%c] %-18s %s",
            info.running ? '*' : ' ',
            info.id,
            info.running ? "RUN" : "IDLE");
    }

    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(0, M5.Display.height() - 10);
    M5.Display.print("[,/.] nav  [Ent] toggle  [` ] back");
}

void ui_module_manager_show(void)
{
    s_cursor = 0;
    s_scroll  = 0;

    // Drain the key that opened this screen
    do { vTaskDelay(pdMS_TO_TICKS(50)); CardputerKb.update(); }
    while (CardputerKb.isPressed());

    while (true) {
        M5.update();
        CardputerKb.update();
        render();

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        auto kb    = CardputerKb.getState();
        char key   = (char)kb.key.key.key_data.keys[0];
        int  count = module_registry_count();
        int  visible = (M5.Display.height() - 24) / LINE_HEIGHT;

        if (key == '`' || key == 27) return;

        if (key == ',' || key == ';') {
            if (s_cursor > 0) {
                s_cursor--;
                if (s_cursor < s_scroll) s_scroll = s_cursor;
            }
        } else if (key == '.' || key == '/') {
            if (s_cursor < count - 1) {
                s_cursor++;
                if (s_cursor >= s_scroll + visible)
                    s_scroll = s_cursor - visible + 1;
            }
        } else if (key == '\n' || key == '\r') {
            module_info_t info;
            if (module_registry_get(s_cursor, &info) == ESP_OK) {
                if (info.running)
                    module_loader_stop(info.id);
                else
                    module_loader_start(info.id);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
