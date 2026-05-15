#include "registry.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "registry";

static module_info_t s_modules[MAX_LOADED_MODULES];
static int           s_count = 0;
static SemaphoreHandle_t s_lock;

void module_registry_init(void)
{
    s_lock  = xSemaphoreCreateMutex();
    s_count = 0;
    memset(s_modules, 0, sizeof(s_modules));
    ESP_LOGI(TAG, "Module registry ready (capacity %d)", MAX_LOADED_MODULES);
}

esp_err_t module_registry_add(const module_info_t *info)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_count >= MAX_LOADED_MODULES) {
        xSemaphoreGive(s_lock);
        ESP_LOGE(TAG, "Registry full (%d modules)", MAX_LOADED_MODULES);
        return ESP_ERR_NO_MEM;
    }
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_modules[i].id, info->id) == 0) {
            xSemaphoreGive(s_lock);
            ESP_LOGE(TAG, "Module '%s' already registered", info->id);
            return ESP_ERR_INVALID_STATE;
        }
    }
    memcpy(&s_modules[s_count], info, sizeof(module_info_t));
    s_count++;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "Registered module '%s' v%s", info->id, info->version);
    return ESP_OK;
}

esp_err_t module_registry_remove(const char *id)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_modules[i].id, id) == 0) {
            memmove(&s_modules[i], &s_modules[i + 1],
                    (s_count - i - 1) * sizeof(module_info_t));
            s_count--;
            xSemaphoreGive(s_lock);
            ESP_LOGI(TAG, "Unregistered module '%s'", id);
            return ESP_OK;
        }
    }
    xSemaphoreGive(s_lock);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t module_registry_get(int index, module_info_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (index < 0 || index >= s_count) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(out, &s_modules[index], sizeof(module_info_t));
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t module_registry_find(const char *id, module_info_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_modules[i].id, id) == 0) {
            if (out) memcpy(out, &s_modules[i], sizeof(module_info_t));
            xSemaphoreGive(s_lock);
            return ESP_OK;
        }
    }
    xSemaphoreGive(s_lock);
    return ESP_ERR_NOT_FOUND;
}

int module_registry_count(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int c = s_count;
    xSemaphoreGive(s_lock);
    return c;
}

bool module_registry_is_running(const char *id)
{
    module_info_t info;
    if (module_registry_find(id, &info) != ESP_OK) return false;
    return info.running;
}

esp_err_t module_registry_set_running(const char *id, bool running)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_modules[i].id, id) == 0) {
            s_modules[i].running = running;
            xSemaphoreGive(s_lock);
            return ESP_OK;
        }
    }
    xSemaphoreGive(s_lock);
    return ESP_ERR_NOT_FOUND;
}
