#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "module_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODULE_ID_MAX_LEN   32
#define MODULE_NAME_MAX_LEN 64
#define MODULE_VER_MAX_LEN  16
#define MAX_LOADED_MODULES  12

typedef void (*module_ui_fn_t)(void);

typedef struct {
    char             id[MODULE_ID_MAX_LEN];
    char             name[MODULE_NAME_MAX_LEN];
    char             version[MODULE_VER_MAX_LEN];
    char             author[MODULE_NAME_MAX_LEN];
    char             category[32];
    bool             running;
    void            *task_handle;
    void            *psram_base;
    size_t           psram_size;
    module_main_fn_t run_fn;
    module_ui_fn_t   ui_fn;
} module_info_t;

void      module_registry_init(void);
esp_err_t module_registry_add(const module_info_t *info);
esp_err_t module_registry_remove(const char *id);
esp_err_t module_registry_get(int index, module_info_t *out);
esp_err_t module_registry_find(const char *id, module_info_t *out);
int       module_registry_count(void);
bool      module_registry_is_running(const char *id);
esp_err_t module_registry_set_running(const char *id, bool running);

#ifdef __cplusplus
}
#endif
