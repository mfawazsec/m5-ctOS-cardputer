/* Mock: nvs_flash.h */
#pragma once
#include "esp_err.h"
extern esp_err_t g_nvs_flash_init_ret;
extern int g_nvs_flash_init_calls;
extern int g_nvs_flash_erase_calls;

static inline esp_err_t nvs_flash_init(void) {
    g_nvs_flash_init_calls++;
    return g_nvs_flash_init_ret;
}
static inline esp_err_t nvs_flash_erase(void) {
    g_nvs_flash_erase_calls++;
    return ESP_OK;
}
