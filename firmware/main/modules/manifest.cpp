#include "manifest.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "manifest";

esp_err_t manifest_parse(const char *json_buf, size_t json_len,
                          module_info_t *out)
{
    cJSON *root = cJSON_ParseWithLength(json_buf, json_len);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse error: %s", cJSON_GetErrorPtr());
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(module_info_t));

    auto get_str = [&](const char *key, char *dst, size_t dst_len) -> bool {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
        if (!cJSON_IsString(item) || !item->valuestring) {
            ESP_LOGE(TAG, "manifest missing required field: %s", key);
            return false;
        }
        strlcpy(dst, item->valuestring, dst_len);
        return true;
    };

    bool ok = true;
    ok &= get_str("id",       out->id,       sizeof(out->id));
    ok &= get_str("name",     out->name,     sizeof(out->name));
    ok &= get_str("version",  out->version,  sizeof(out->version));
    ok &= get_str("author",   out->author,   sizeof(out->author));
    ok &= get_str("category", out->category, sizeof(out->category));

    cJSON_Delete(root);

    if (!ok) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Parsed manifest: id=%s name=%s v%s",
             out->id, out->name, out->version);
    return ESP_OK;
}
