#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_psram.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"

#include "settings/config.h"
#include "wifi/hotspot.h"
#include "wifi/webserver.h"
#include "ui/menu.h"
#include "ui/memory_view.h"
#include "modules/registry.h"
#include "modules/loader.h"
#include "cardputer_keyboard.h"

static const char *TAG = "ctOS";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "==============================");
    ESP_LOGI(TAG, " m5-ctOS v2.0  boot sequence ");
    ESP_LOGI(TAG, "==============================");

    /* ── [1/9] NVS ───────────────────────────────────────────────────────── */
    ESP_LOGI(TAG, "[1/9] NVS flash init...");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "  NVS dirty — erasing and re-initialising");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "[1/9] NVS OK");

    /* ── [2/9] PSRAM ─────────────────────────────────────────────────────── */
    ESP_LOGI(TAG, "[2/9] PSRAM check...");
    if (esp_psram_is_initialized()) {
        ESP_LOGI(TAG, "[2/9] PSRAM: %zu KB available", esp_psram_get_size() / 1024);
    } else {
        ESP_LOGW(TAG, "[2/9] PSRAM not initialised");
    }

    /* ── [3/9] Config ────────────────────────────────────────────────────── */
    ESP_LOGI(TAG, "[3/9] config_init...");
    config_init();
    ESP_LOGI(TAG, "[3/9] config OK");

    /* ── [3b] Internal storage ───────────────────────────────────────────── */
    // Mount SPIFFS "storage" partition at /spiffs (general file storage)
    {
        esp_vfs_spiffs_conf_t cfg = {
            .base_path              = "/spiffs",
            .partition_label        = "storage",
            .max_files              = 20,
            .format_if_mount_failed = false,
        };
        esp_err_t e = esp_vfs_spiffs_register(&cfg);
        if (e == ESP_OK)
            ESP_LOGI(TAG, "[3b] SPIFFS mounted at /spiffs");
        else
            ESP_LOGW(TAG, "[3b] SPIFFS mount failed: %s", esp_err_to_name(e));
    }

    // Mount "modules" FAT partition at /modules
    {
        static wl_handle_t wl_handle = WL_INVALID_HANDLE;
        esp_vfs_fat_mount_config_t fat_cfg = {
            .format_if_mount_failed = false,
            .max_files              = 20,
            .allocation_unit_size   = 0,
        };
        esp_err_t e = esp_vfs_fat_spiflash_mount_rw_wl(
            "/modules", "modules", &fat_cfg, &wl_handle);
        if (e == ESP_OK)
            ESP_LOGI(TAG, "[3b] FAT modules partition mounted at /modules");
        else
            ESP_LOGW(TAG, "[3b] FAT modules mount failed: %s", esp_err_to_name(e));
    }

    /* ── [4/9] Module registry + loader ─────────────────────────────────── */
    ESP_LOGI(TAG, "[4/9] module_registry_init...");
    module_registry_init();
    ESP_LOGI(TAG, "[4/9] module_loader_init...");
    module_loader_init();
    ESP_LOGI(TAG, "[4/9] modules OK");

    /* ── [5/9] Display / M5.begin ────────────────────────────────────────── */
    ESP_LOGI(TAG, "[5/9] ui_menu_init (M5.begin + display)...");
    ui_menu_init();
    ESP_LOGI(TAG, "[5/9] display OK");

    /* ── [6/9] Keyboard ──────────────────────────────────────────────────── */
    ESP_LOGI(TAG, "[6/9] keyboard init (TCA8418 I2C probe)...");
    bool kb_ok = CardputerKb.init();
    if (kb_ok) {
        ESP_LOGI(TAG, "[6/9] keyboard OK");
    } else {
        ESP_LOGW(TAG, "[6/9] keyboard NOT found — continuing without keyboard");
    }

    /* ── [7/9] Memory view ───────────────────────────────────────────────── */
    ESP_LOGI(TAG, "[7/9] memory_view_init...");
    memory_view_init();
    ESP_LOGI(TAG, "[7/9] memory view OK");

    /* ── [8/9] WiFi AP ───────────────────────────────────────────────────── */
    ESP_LOGI(TAG, "[8/9] wifi AP check (enabled=%d)...",
             config_get_wifi_ap_enabled());
    if (config_get_wifi_ap_enabled()) {
        ESP_LOGI(TAG, "[8/9] hotspot_start...");
        hotspot_start();
        ESP_LOGI(TAG, "[8/9] webserver_start...");
        webserver_start();
        ESP_LOGI(TAG, "[8/9] WiFi AP OK");
    } else {
        ESP_LOGI(TAG, "[8/9] WiFi AP disabled — skipped");
    }

    /* ── [9/9] Module autoload ───────────────────────────────────────────── */
    ESP_LOGI(TAG, "[9/9] module_loader_autoload...");
    module_loader_autoload();
    ESP_LOGI(TAG, "[9/9] autoload OK");

    /* ── Boot complete ───────────────────────────────────────────────────── */
    ESP_LOGI(TAG, "==============================");
    ESP_LOGI(TAG, " Boot complete!");
    ESP_LOGI(TAG, " Free heap : %lu KB", esp_get_free_heap_size() / 1024);
    ESP_LOGI(TAG, " Min heap  : %lu KB", esp_get_minimum_free_heap_size() / 1024);
    ESP_LOGI(TAG, "==============================");

    ui_menu_run();
}
