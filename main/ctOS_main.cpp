#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_psram.h"

#include "settings/config.h"
#include "wifi/hotspot.h"
#include "wifi/webserver.h"
#include "ui/menu.h"
#include "ui/memory_view.h"
#include "modules/registry.h"
#include "modules/loader.h"

static const char *TAG = "ctOS";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "m5-ctOS v2.0 booting...");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    if (esp_psram_is_initialized()) {
        ESP_LOGI(TAG, "PSRAM: %zu KB available", esp_psram_get_size() / 1024);
    }

    config_init();
    module_registry_init();
    module_loader_init();

    ui_menu_init();
    memory_view_init();

    if (config_get_wifi_ap_enabled()) {
        hotspot_start();
        webserver_start();
    }

    module_loader_autoload();

    ESP_LOGI(TAG, "Boot complete. Free heap: %lu KB",
             esp_get_free_heap_size() / 1024);

    ui_menu_run();
}
