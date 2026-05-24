#include "loader.h"
#include "registry.h"
#include "manifest.h"
#include "module_api.h"
#include "module_log.h"
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

// ---------------------------------------------------------------------------
// Forward declarations for all statically-compiled module entry points
// ---------------------------------------------------------------------------
extern "C" {
esp_err_t badusb_main(const ctos_api_t *api);
esp_err_t ble_hid_inject_main(const ctos_api_t *api);
esp_err_t blerp_main(const ctos_api_t *api);
esp_err_t espnow_c2_main(const ctos_api_t *api);
esp_err_t gairoscope_main(const ctos_api_t *api);
esp_err_t ir_dazzle_main(const ctos_api_t *api);
esp_err_t nuit_inject_main(const ctos_api_t *api);
esp_err_t passive_keystroke_main(const ctos_api_t *api);
esp_err_t passive_wifi_csi_main(const ctos_api_t *api);
esp_err_t sonar_snoop_main(const ctos_api_t *api);
esp_err_t ult_jammer_main(const ctos_api_t *api);
esp_err_t wiki_eve_main(const ctos_api_t *api);

void badusb_ui_show(void);
void ble_hid_inject_ui_show(void);
void blerp_ui_show(void);
void espnow_c2_ui_show(void);
void gairoscope_ui_show(void);
void ir_dazzle_ui_show(void);
void nuit_inject_ui_show(void);
void passive_keystroke_ui_show(void);
void passive_wifi_csi_ui_show(void);
void sonar_snoop_ui_show(void);
void ult_jammer_ui_show(void);
void wiki_eve_ui_show(void);
}

// ---------------------------------------------------------------------------
// Builtin run_fn / ui_fn table
// ---------------------------------------------------------------------------
typedef struct {
    const char      *id;
    module_main_fn_t run_fn;
    module_ui_fn_t   ui_fn;
} builtin_entry_t;

static const builtin_entry_t s_builtin_fns[] = {
    { "badusb",           badusb_main,           badusb_ui_show           },
    { "ble_hid_inject",   ble_hid_inject_main,   ble_hid_inject_ui_show   },
    { "blerp",            blerp_main,             blerp_ui_show            },
    { "espnow_c2",        espnow_c2_main,         espnow_c2_ui_show        },
    { "gairoscope",       gairoscope_main,        gairoscope_ui_show       },
    { "ir_dazzle",        ir_dazzle_main,         ir_dazzle_ui_show        },
    { "nuit_inject",      nuit_inject_main,       nuit_inject_ui_show      },
    { "passive_keystroke",passive_keystroke_main, passive_keystroke_ui_show},
    { "passive_wifi_csi", passive_wifi_csi_main,  passive_wifi_csi_ui_show },
    { "sonar_snoop",      sonar_snoop_main,       sonar_snoop_ui_show      },
    { "ult_jammer",       ult_jammer_main,        ult_jammer_ui_show       },
    { "wiki_eve",         wiki_eve_main,          wiki_eve_ui_show         },
};

// ---------------------------------------------------------------------------
// IPC API implementation provided to modules
// ---------------------------------------------------------------------------
static void api_log(const char *id, const char *msg)
{
    char tag[48];
    snprintf(tag, sizeof(tag), "mod/%s", id);
    ESP_LOGI(tag, "%s", msg);
    module_log_push(id, msg);
}

static void api_display_print(const char *id, const char *line)
{
    module_log_push(id, line);
    char tag[48];
    snprintf(tag, sizeof(tag), "mod/%s", id);
    ESP_LOGI(tag, "DISP: %s", line);
}

static void api_display_clear(const char *id)
{
    module_log_clear(id);
}

static esp_err_t api_send_msg(const char *id, ctos_msg_t *msg)
{
    (void)id; (void)msg;
    return ESP_OK;
}

static void *api_psram_alloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_DEFAULT);  // fallback if no PSRAM
    return p;
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

const ctos_api_t *module_loader_get_api(void) { return &s_api; }

// ---------------------------------------------------------------------------
// Built-in module manifests — registered at boot
// ---------------------------------------------------------------------------
static const char *const s_builtin_manifests[] = {
    "{\"id\":\"badusb\",\"name\":\"Interactive BadUSB (DuckyScript)\",\"version\":\"1.0.0\",\"author\":\"fawaz\",\"category\":\"hid\"}",
    "{\"id\":\"ble_hid_inject\",\"name\":\"BLE HID Wireless Keyboard Injection\",\"version\":\"1.0.0\",\"author\":\"fawaz\",\"category\":\"ble\"}",
    "{\"id\":\"blerp\",\"name\":\"BLERP BLE Re-Pairing Attack\",\"version\":\"1.0.0\",\"author\":\"fawaz\",\"category\":\"ble\"}",
    "{\"id\":\"espnow_c2\",\"name\":\"ESP-NOW Covert C2 Channel\",\"version\":\"1.0.0\",\"author\":\"fawaz\",\"category\":\"wifi\"}",
    "{\"id\":\"gairoscope\",\"name\":\"GAIROSCOPE Speaker-to-Gyroscope Covert Channel\",\"version\":\"1.0.0\",\"author\":\"fawaz\",\"category\":\"acoustic\"}",
    "{\"id\":\"ir_dazzle\",\"name\":\"IR Camera Dazzling\",\"version\":\"1.0.0\",\"author\":\"fawaz\",\"category\":\"ir\"}",
    "{\"id\":\"nuit_inject\",\"name\":\"NUIT Near-Ultrasound Voice Injection\",\"version\":\"1.0.0\",\"author\":\"fawaz\",\"category\":\"acoustic\"}",
    "{\"id\":\"passive_keystroke\",\"name\":\"Passive Acoustic Keystroke Logger\",\"version\":\"1.0.0\",\"author\":\"fawaz\",\"category\":\"acoustic\"}",
    "{\"id\":\"passive_wifi_csi\",\"name\":\"Passive WiFi Sensing (CSI)\",\"version\":\"1.0.0\",\"author\":\"fawaz\",\"category\":\"passive-wifi\"}",
    "{\"id\":\"sonar_snoop\",\"name\":\"SonarSnoop Acoustic Gesture Inference\",\"version\":\"1.0.0\",\"author\":\"fawaz\",\"category\":\"acoustic\"}",
    "{\"id\":\"ult_jammer\",\"name\":\"Ultrasonic Microphone Jammer\",\"version\":\"1.0.0\",\"author\":\"fawaz\",\"category\":\"acoustic\"}",
    "{\"id\":\"wiki_eve\",\"name\":\"WiKI-Eve BFI Keystroke Inference\",\"version\":\"1.0.0\",\"author\":\"fawaz\",\"category\":\"passive-wifi\"}",
};

static void scan_module_dir(const char *base)
{
    DIR *d = opendir(base);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        char path[384];
        snprintf(path, sizeof(path), "%s/%s", base, ent->d_name);
        char mfst[512];
        snprintf(mfst, sizeof(mfst), "%s/manifest.json", path);
        FILE *f = fopen(mfst, "r");
        if (!f) continue;
        fclose(f);
        esp_err_t e = module_loader_install(path);
        if (e == ESP_OK)
            ESP_LOGI(TAG, "Installed extra module from %s", path);
    }
    closedir(d);
}

void module_loader_init(void)
{
    size_t n_builtins = sizeof(s_builtin_manifests) / sizeof(s_builtin_manifests[0]);
    size_t n_fns      = sizeof(s_builtin_fns) / sizeof(s_builtin_fns[0]);

    for (size_t i = 0; i < n_builtins; i++) {
        const char *js = s_builtin_manifests[i];
        module_info_t info = {};
        if (manifest_parse(js, strlen(js), &info) != ESP_OK) continue;

        for (size_t j = 0; j < n_fns; j++) {
            if (strcmp(info.id, s_builtin_fns[j].id) == 0) {
                info.run_fn = s_builtin_fns[j].run_fn;
                info.ui_fn  = s_builtin_fns[j].ui_fn;
                break;
            }
        }
        module_registry_add(&info);
    }

    scan_module_dir("/modules");
    scan_module_dir("/sdcard/modules");

    ESP_LOGI(TAG, "Module loader ready — %d module(s) registered",
             module_registry_count());
}

esp_err_t module_loader_install(const char *ctm_path)
{
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

esp_err_t module_loader_start(const char *module_id)
{
    if (module_registry_is_running(module_id)) return ESP_OK;

    module_info_t info = {};
    if (module_registry_find(module_id, &info) != ESP_OK) {
        ESP_LOGE(TAG, "Module '%s' not found", module_id);
        return ESP_ERR_NOT_FOUND;
    }
    if (!info.run_fn) {
        ESP_LOGW(TAG, "Module '%s' has no run_fn", module_id);
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGI(TAG, "Starting module '%s'", module_id);
    return info.run_fn(&s_api);
}

esp_err_t module_loader_stop(const char *module_id)
{
    if (!module_registry_is_running(module_id)) return ESP_OK;
    module_registry_set_running(module_id, false);
    vTaskDelay(pdMS_TO_TICKS(200));
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
