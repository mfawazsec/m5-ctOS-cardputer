#include "loader.h"
#include "registry.h"
#include "manifest.h"
#include "module_api.h"
#include "settings/config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_vfs_fat.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>

static const char *TAG = "loader";

// ctOS IPC API implementation provided to modules
static void api_log(const char *id, const char *msg)
{
    char tag[48];
    snprintf(tag, sizeof(tag), "mod/%s", id);
    ESP_LOGI(tag, "%s", msg);
}

static void api_display_print(const char *id, const char *line)
{
    // Delegate to memory_view overlay
    char tag[48];
    snprintf(tag, sizeof(tag), "mod/%s", id);
    ESP_LOGI(tag, "DISP: %s", line);
}

static void api_display_clear(const char *id)
{
    (void)id;
}

static esp_err_t api_send_msg(const char *id, ctos_msg_t *msg)
{
    (void)id; (void)msg;
    return ESP_OK;
}

static void *api_psram_alloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void api_psram_free(void *ptr)
{
    heap_caps_free(ptr);
}

static uint32_t api_get_free_psram(void)
{
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

static uint32_t api_get_free_heap(void)
{
    return (uint32_t)esp_get_free_heap_size();
}

static const ctos_api_t s_api = {
    .log            = api_log,
    .display_print  = api_display_print,
    .display_clear  = api_display_clear,
    .send_msg       = api_send_msg,
    .psram_alloc    = api_psram_alloc,
    .psram_free     = api_psram_free,
    .get_free_psram = api_get_free_psram,
    .get_free_heap  = api_get_free_heap,
};

void module_loader_init(void)
{
    ESP_LOGI(TAG, "Module loader ready");
}

esp_err_t module_loader_install(const char *ctm_path)
{
    // .ctm is a ZIP. Extraction handled offline; on SD card we expect:
    //   /sdcard/modules/<id>/manifest.json
    //   /sdcard/modules/<id>/firmware.bin
    // This function reads manifest.json and registers the module.
    char manifest_path[256];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", ctm_path);

    FILE *f = fopen(manifest_path, "r");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", manifest_path);
        return ESP_ERR_NOT_FOUND;
    }

    char buf[1024];
    size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[len] = '\0';

    module_info_t info = {};
    esp_err_t err = manifest_parse(buf, len, &info);
    if (err != ESP_OK) return err;

    return module_registry_add(&info);
}

typedef struct {
    char module_id[MODULE_ID_MAX_LEN];
} loader_task_arg_t;

static void module_task(void *arg)
{
    loader_task_arg_t *a = (loader_task_arg_t *)arg;
    ESP_LOGI(TAG, "Module task started: %s", a->module_id);

    module_registry_set_running(a->module_id, true);

    // Real implementation: load firmware.bin into PSRAM, map, call entrypoint.
    // Placeholder: loop until signalled to stop.
    while (module_registry_is_running(a->module_id)) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "Module task exiting: %s", a->module_id);
    free(a);
    vTaskDelete(NULL);
}

esp_err_t module_loader_start(const char *module_id)
{
    if (module_registry_is_running(module_id)) {
        ESP_LOGW(TAG, "Module '%s' already running", module_id);
        return ESP_ERR_INVALID_STATE;
    }

    loader_task_arg_t *arg = (loader_task_arg_t *)malloc(sizeof(loader_task_arg_t));
    strlcpy(arg->module_id, module_id, sizeof(arg->module_id));

    TaskHandle_t handle;
    BaseType_t ret = xTaskCreatePinnedToCore(
        module_task, module_id, 8192, arg, 5, &handle, 1);

    if (ret != pdPASS) {
        free(arg);
        ESP_LOGE(TAG, "Failed to create task for module '%s'", module_id);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Started module '%s'", module_id);
    return ESP_OK;
}

esp_err_t module_loader_stop(const char *module_id)
{
    if (!module_registry_is_running(module_id)) return ESP_OK;
    module_registry_set_running(module_id, false);
    // Task will exit its loop and call vTaskDelete itself
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "Stopped module '%s'", module_id);
    return ESP_OK;
}

void module_loader_autoload(void)
{
    const char *list = config_get_autoload_list();
    if (!list || list[0] == '\0') return;

    char buf[256];
    strlcpy(buf, list, sizeof(buf));

    char *id = strtok(buf, ",");
    while (id) {
        ESP_LOGI(TAG, "Autoloading module: %s", id);
        module_loader_start(id);
        id = strtok(NULL, ",");
    }
}
