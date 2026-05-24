#include "menu.h"
#include "boot_banner.h"
#include "memory_view.h"
#include "file_browser.h"
#include "module_manager.h"
#include "wifi_config.h"
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
    M5.Display.setTextSize(1.5f);
    ESP_LOGI(TAG, "Display init: %dx%d",
             (int)M5.Display.width(), (int)M5.Display.height());

    /* Show boot splash immediately after display is ready */
    boot_banner_show();
}

// Key character → action mapping (Cardputer QWERTY keyboard)
static void handle_key(char key)
{
    switch (key) {
    case 'M': case 'm':
        ui_module_manager_show();
        break;
    case 'F': case 'f':
        // NULL triggers location picker (SD card / internal flash)
        ui_file_browser_show(NULL);
        break;
    case 'S': case 's':
        ui_wifi_config_show();
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
            char key = (char)kb.key.key.key_data.keys[0];
            if (key != 0) {
                handle_key(key);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(33)); // ~30 fps
    }
}
