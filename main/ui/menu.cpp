#include "menu.h"
#include "memory_view.h"
#include "file_browser.h"
#include "module_manager.h"
#include "wifi/hotspot.h"
#include "settings/config.h"
#include "esp_log.h"
#include "M5Unified.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cardputer_keyboard.h"

static const char *TAG = "menu";

void ui_menu_init(void)
{
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);
    M5.Display.setBrightness(config_get_brightness());
    M5.Display.setTextSize(1);
    ESP_LOGI(TAG, "Display init: %dx%d",
             (int)M5.Display.width(), (int)M5.Display.height());
}

// Key character → action mapping (Cardputer QWERTY keyboard)
static void handle_key(char key)
{
    switch (key) {
    case 'M': case 'm':
        ui_module_manager_show();
        break;
    case 'F': case 'f':
        ui_file_browser_show("/sdcard");
        break;
    case 'S': case 's':
        // Settings handled by web UI; show reminder on screen
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setCursor(0, 0);
        M5.Display.println("Settings: visit 192.168.4.1");
        M5.Display.println("Press any key to return.");
        while (!CardputerKb.isChange()) vTaskDelay(pdMS_TO_TICKS(50));
        break;
    case 'W': case 'w':
        if (hotspot_is_running()) {
            hotspot_stop();
        } else {
            hotspot_start();
        }
        break;
    default:
        break;
    }
}

void ui_menu_run(void)
{
    ESP_LOGI(TAG, "Entering main UI loop");

    while (true) {
        M5.update();
        CardputerKb.update();

        memory_view_render();

        if (CardputerKb.isChange() && CardputerKb.isPressed()) {
            auto kb = CardputerKb.getState();
            if (kb.key.key.opt_key.fn == 0 && kb.key.key.key_data.keys[0] != 0) {
                handle_key((char)kb.key.key.key_data.keys[0]);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(33)); // ~30 fps
    }
}
