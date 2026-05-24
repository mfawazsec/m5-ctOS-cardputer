/**
 * boot_sim.c — Implementation of the ctOS boot phase simulation.
 *
 * Compiled for Linux host using mock ESP-IDF headers.
 * Contains inline reimplementations of the real subsystem logic
 * so tests can exercise it without the actual ESP-IDF toolchain.
 */

#include "boot_sim.h"
#include "../stubs/mock_globals.h"

/* Pull in mock headers (gcc -I tests/host/mocks resolves these) */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* ── Internal registry (mirrors registry.cpp) ─────────────────────── */
#define MODULE_ID_MAX_LEN   32
#define MODULE_VER_MAX_LEN  16
#define MAX_LOADED_MODULES  16

typedef struct {
    char id[MODULE_ID_MAX_LEN];
    char version[MODULE_VER_MAX_LEN];
    bool running;
} sim_module_t;

static sim_module_t s_modules[MAX_LOADED_MODULES];
static int          s_mod_count = 0;
static SemaphoreHandle_t s_lock = NULL;

/* ── Internal config state (mirrors config.cpp) ───────────────────── */
static char   s_ssid[33]     = {0};
static char   s_pass[64]     = {0};
static bool   s_ap_enabled   = true;
static uint8_t s_brightness  = 128;

/* ── Internal hotspot call counter ───────────────────────────────── */
static int s_hotspot_start_calls = 0;

/* ─────────────────────────────────────────────────────────────────── */

/* [1/9] NVS */
int boot_phase_nvs(void)
{
    ESP_LOGI("ctOS", "[1/9] NVS flash init...");

    /* On dirty state, init returns error code and we erase+reinit */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW("ctOS", "  NVS dirty — erasing and re-initialising");
        nvs_flash_erase();
        /* Reset mock return value so second call succeeds */
        g_nvs_flash_init_ret = ESP_OK;
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;
    ESP_LOGI("ctOS", "[1/9] NVS OK");
    return 0;
}

/* [2/9] PSRAM */
void boot_phase_psram(void)
{
    ESP_LOGI("ctOS", "[2/9] PSRAM check...");
    if (esp_psram_is_initialized()) {
        ESP_LOGI("ctOS", "[2/9] PSRAM: %zu KB available",
                 esp_psram_get_size() / 1024);
    } else {
        ESP_LOGW("ctOS", "[2/9] PSRAM not initialised");
    }
}

/* [3/9] Config */
void boot_phase_config(void)
{
    ESP_LOGI("ctOS", "[3/9] config_init...");

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("ctos_cfg", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE("ctOS", "nvs_open failed");
        return;
    }

    /* Build default SSID from fake MAC */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char default_ssid[33];
    snprintf(default_ssid, sizeof(default_ssid), "ctOS-%02X%02X%02X",
             mac[3], mac[4], mac[5]);

    size_t len = sizeof(s_ssid);
    if (nvs_get_str(nvs, "ssid", s_ssid, &len) != ESP_OK)
        strlcpy(s_ssid, default_ssid, sizeof(s_ssid));

    len = sizeof(s_pass);
    if (nvs_get_str(nvs, "pass", s_pass, &len) != ESP_OK)
        strlcpy(s_pass, "ctOS2024!", sizeof(s_pass));

    uint8_t ap_en = 1;
    if (nvs_get_u8(nvs, "ap_en", &ap_en) != ESP_OK) ap_en = 1;
    s_ap_enabled = (ap_en != 0);

    if (nvs_get_u8(nvs, "brightness", &s_brightness) != ESP_OK)
        s_brightness = 128;

    ESP_LOGI("ctOS", "[3/9] config OK — SSID: %s, AP: %s",
             s_ssid, s_ap_enabled ? "on" : "off");
}

/* [4/9] Registry */
void boot_phase_registry(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_mod_count = 0;
    memset(s_modules, 0, sizeof(s_modules));
    ESP_LOGI("ctOS", "[4/9] module_registry_init — capacity %d",
             MAX_LOADED_MODULES);
}

/* [5/9] Loader */
void boot_phase_loader(void)
{
    ESP_LOGI("ctOS", "[4/9] module_loader_init...");
    /* No hardware init needed on host */
}

/* [6/9] Keyboard */
bool boot_phase_keyboard(bool force_fail)
{
    ESP_LOGI("ctOS", "[6/9] keyboard init (TCA8418 I2C probe)...");
    if (force_fail) {
        ESP_LOGW("ctOS", "[6/9] keyboard NOT found — continuing without keyboard");
        return false;
    }
    ESP_LOGI("ctOS", "[6/9] keyboard OK");
    return true;
}

/* [8/9] WiFi */
int boot_phase_wifi(void)
{
    s_hotspot_start_calls = 0;
    ESP_LOGI("ctOS", "[8/9] wifi AP check (enabled=%d)...", s_ap_enabled);
    if (s_ap_enabled) {
        ESP_LOGI("ctOS", "[8/9] hotspot_start...");
        s_hotspot_start_calls++;
        ESP_LOGI("ctOS", "[8/9] WiFi AP OK");
    } else {
        ESP_LOGI("ctOS", "[8/9] WiFi AP disabled — skipped");
    }
    return s_hotspot_start_calls;
}

/* [9/9] Autoload */
void boot_phase_autoload(const char *list)
{
    if (!list || list[0] == '\0') return;

    char buf[256];
    strlcpy(buf, list, sizeof(buf));

    char *id = strtok(buf, ",");
    while (id) {
        ESP_LOGI("ctOS", "[9/9] autoloading: %s", id);
        boot_loader_start(id);
        id = strtok(NULL, ",");
    }
}

/* ── Full sequence ───────────────────────────────────────────────── */
int boot_run_full_sequence(void)
{
    int r;
    if ((r = boot_phase_nvs()) != 0) return r;
    boot_phase_psram();
    boot_phase_config();
    boot_phase_registry();
    boot_phase_loader();
    boot_phase_keyboard(false);
    /* Phase 7: memory_view (no-op on host) */
    boot_phase_wifi();
    boot_phase_autoload("");
    ESP_LOGI("ctOS", "Boot complete! Free heap: %lu KB",
             (unsigned long)esp_get_free_heap_size() / 1024);
    return 0;
}

/* ── Registry helpers ────────────────────────────────────────────── */
int boot_registry_add(const char *id, const char *version)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_mod_count >= MAX_LOADED_MODULES) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NO_MEM;
    }
    for (int i = 0; i < s_mod_count; i++) {
        if (strcmp(s_modules[i].id, id) == 0) {
            xSemaphoreGive(s_lock);
            return ESP_ERR_INVALID_STATE;
        }
    }
    strlcpy(s_modules[s_mod_count].id,      id,      MODULE_ID_MAX_LEN);
    strlcpy(s_modules[s_mod_count].version,  version, MODULE_VER_MAX_LEN);
    s_modules[s_mod_count].running = false;
    s_mod_count++;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

int boot_get_module_count(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int c = s_mod_count;
    xSemaphoreGive(s_lock);
    return c;
}

int boot_registry_get(int index, void *out)
{
    (void)out;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (index < 0 || index >= s_mod_count) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

int boot_loader_start(const char *id)
{
    /* Check module is registered */
    bool found = false;
    for (int i = 0; i < s_mod_count; i++) {
        if (strcmp(s_modules[i].id, id) == 0) { found = true; break; }
    }
    if (!found) return ESP_ERR_NOT_FOUND;

    /* Allocate task arg */
    char *arg = (char *)malloc(32);
    strlcpy(arg, id, 32);

    TaskHandle_t h;
    BaseType_t ret = xTaskCreatePinnedToCore(NULL, id, 8192, arg, 5, &h, 1);
    if (ret != pdPASS) {
        free(arg);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ── Config accessors ────────────────────────────────────────────── */
const char *boot_get_ssid(void)        { return s_ssid; }
const char *boot_get_password(void)    { return s_pass; }
bool        boot_get_ap_enabled(void)  { return s_ap_enabled; }
void        boot_set_ap_enabled(bool e){ s_ap_enabled = e; }
