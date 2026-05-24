#pragma once
#include "registry.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t manifest_parse(const char *json_buf, size_t json_len,
                          module_info_t *out);

#ifdef __cplusplus
}
#endif
