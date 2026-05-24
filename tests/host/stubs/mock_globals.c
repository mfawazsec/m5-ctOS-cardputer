/*
 * mock_globals.c — Mutable globals used by mock headers.
 * All tests link this file to control mock behaviour.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* esp_log */
int  g_log_count = 0;
char g_last_log[512] = {0};

/* nvs_flash */
esp_err_t g_nvs_flash_init_ret = 0; /* ESP_OK */
int g_nvs_flash_init_calls = 0;
int g_nvs_flash_erase_calls = 0;

/* nvs */
esp_err_t g_nvs_open_ret = 0; /* ESP_OK */

/* psram */
bool   g_psram_initialized = true;
size_t g_psram_size = 8 * 1024 * 1024;

/* system */
uint32_t g_free_heap     = 200 * 1024;
uint32_t g_min_free_heap = 150 * 1024;

/* freertos/task */
int       g_task_create_calls = 0;
int       g_task_create_ret   = 1; /* pdPASS */

/* Reset all mock state between tests */
void mock_reset_all(void)
{
    g_log_count            = 0;
    g_last_log[0]          = '\0';
    g_nvs_flash_init_ret   = 0;
    g_nvs_flash_init_calls = 0;
    g_nvs_flash_erase_calls= 0;
    g_nvs_open_ret         = 0;
    g_psram_initialized    = true;
    g_psram_size           = 8 * 1024 * 1024;
    g_free_heap            = 200 * 1024;
    g_min_free_heap        = 150 * 1024;
    g_task_create_calls    = 0;
    g_task_create_ret      = 1;
}
