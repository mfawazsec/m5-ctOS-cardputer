#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_psram.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "sdmmc_cmd.h"
#include <sys/stat.h>

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

    /* ── [0] GPIO5 HIGH — SD power enable on CardputerADV ────────────────── */
    {
        gpio_config_t io5 = {
            .pin_bit_mask = (1ULL << 5),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&io5);
        gpio_set_level(GPIO_NUM_5, 1);
        ESP_LOGI(TAG, "[0] GPIO5 HIGH (SD power)");
    }

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
            .format_if_mount_failed = true,
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
            .format_if_mount_failed = true,
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

    /* ── [5b] SD card (after M5.begin — M5GFX owns SPI2_HOST during autodetect) */
    /* LFN uses stack (CONFIG_FATFS_LFN_STACK) so no heap alloc needed here.     */
    /* SPI2_HOST: SCK=GPIO40, MOSI=GPIO14, MISO=GPIO39, CS=GPIO12                */
    {
        static sdmmc_card_t *s_sd_card = nullptr;
        spi_bus_config_t spi_bus = {
            .mosi_io_num     = GPIO_NUM_14,
            .miso_io_num     = GPIO_NUM_39,
            .sclk_io_num     = GPIO_NUM_40,
            .quadwp_io_num   = -1,
            .quadhd_io_num   = -1,
            .max_transfer_sz = 4096,
        };
        esp_err_t e = spi_bus_initialize(SPI2_HOST, &spi_bus, SPI_DMA_CH_AUTO);
        if (e == ESP_OK || e == ESP_ERR_INVALID_STATE) {
            sdmmc_host_t         host    = SDSPI_HOST_DEFAULT();
            host.slot                     = SPI2_HOST;
            sdspi_device_config_t dev_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
            dev_cfg.host_id               = (spi_host_device_t)SPI2_HOST;
            dev_cfg.gpio_cs               = GPIO_NUM_12;
            esp_vfs_fat_sdmmc_mount_config_t fat_mnt = {
                .format_if_mount_failed = false,
                .max_files              = 8,
                .allocation_unit_size   = 0,
            };
            e = esp_vfs_fat_sdspi_mount("/sdcard", &host, &dev_cfg, &fat_mnt, &s_sd_card);
            if (e == ESP_OK) {
                ESP_LOGI(TAG, "[5b] SD card mounted at /sdcard");
                mkdir("/sdcard/payloads",    0755);
                mkdir("/sdcard/keystrokes",  0755);
                mkdir("/sdcard/csi",         0755);
            } else {
                ESP_LOGW(TAG, "[5b] SD card not found: %s", esp_err_to_name(e));
            }
        } else {
            ESP_LOGW(TAG, "[5b] SPI2 bus init failed: %s", esp_err_to_name(e));
        }
    }

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
