#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void      module_loader_init(void);
esp_err_t module_loader_install(const char *ctm_path);
esp_err_t module_loader_start(const char *module_id);
esp_err_t module_loader_stop(const char *module_id);
void      module_loader_autoload(void);

#ifdef __cplusplus
}
#endif
