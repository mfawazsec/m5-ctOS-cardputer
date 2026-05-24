#include "module_manager.h"
#include "modules/registry.h"
#include "modules/loader.h"
#include "esp_log.h"
#include "M5Unified.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cardputer_keyboard.h"


static int         s_cursor = 0;

static void render(void)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.print("Module Manager\n");

    int count = module_registry_count();
    if (count == 0) {
        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.print("  No modules installed.\n");
        M5.Display.print("  Upload .ctm via web UI.");
    }

    for (int i = 0; i < count; i++) {
        module_info_t info;
        if (module_registry_get(i, &info) != ESP_OK) continue;
        bool selected = (i == s_cursor);

        M5.Display.setTextColor(selected ? TFT_BLACK : TFT_WHITE,
                                selected ? TFT_WHITE : TFT_BLACK);
        M5.Display.printf("  [%c] %-20s %-8s\n",
            info.running ? '*' : ' ',
            info.id,
            info.running ? "RUNNING" : "IDLE");
    }

    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(0, M5.Display.height() - 10);
    M5.Display.print("[UP/DN] nav  [OK] toggle  [ESC] back");
}

void ui_module_manager_show(void)
{
    s_cursor = 0;
    int count = module_registry_count();

    while (true) {
        M5.update();
        CardputerKb.update();
        render();

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        auto kb  = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];

        if (key == '`' || key == 27) return; // backtick or ESC

        if (kb.key.key.opt_key.fn) {
            if (kb.key.key.key_data.keys[0] == 'i' && s_cursor > 0)
                s_cursor--;
            else if (kb.key.key.key_data.keys[0] == 'k' && s_cursor < count - 1)
                s_cursor++;
        } else if (key == '\n' || key == '\r') {
            module_info_t info;
            if (module_registry_get(s_cursor, &info) == ESP_OK) {
                if (info.running)
                    module_loader_stop(info.id);
                else
                    module_loader_start(info.id);
                count = module_registry_count();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
