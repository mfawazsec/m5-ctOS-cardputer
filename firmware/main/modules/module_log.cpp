#include "module_log.h"
#include "registry.h"
#include "esp_err.h"
#include <string.h>

typedef struct {
    char lines[MOD_LOG_LINES][MOD_LOG_LINE_LEN];
    int  head;
    int  count;
} mod_log_buf_t;

static mod_log_buf_t s_bufs[MAX_LOADED_MODULES];

static int find_idx(const char *id)
{
    module_info_t info;
    int n = module_registry_count();
    for (int i = 0; i < n; i++) {
        if (module_registry_get(i, &info) == ESP_OK &&
            strncmp(info.id, id, MODULE_ID_MAX_LEN) == 0)
            return i;
    }
    return -1;
}

void module_log_push(const char *module_id, const char *line)
{
    int idx = find_idx(module_id);
    if (idx < 0 || idx >= MAX_LOADED_MODULES) return;
    mod_log_buf_t *b = &s_bufs[idx];
    strncpy(b->lines[b->head], line, MOD_LOG_LINE_LEN - 1);
    b->lines[b->head][MOD_LOG_LINE_LEN - 1] = '\0';
    b->head = (b->head + 1) % MOD_LOG_LINES;
    if (b->count < MOD_LOG_LINES) b->count++;
}

int module_log_count(const char *module_id)
{
    int idx = find_idx(module_id);
    if (idx < 0 || idx >= MAX_LOADED_MODULES) return 0;
    return s_bufs[idx].count;
}

bool module_log_get(const char *module_id, int line_idx, char *out, size_t out_len)
{
    int idx = find_idx(module_id);
    if (idx < 0 || idx >= MAX_LOADED_MODULES) return false;
    mod_log_buf_t *b = &s_bufs[idx];
    if (line_idx < 0 || line_idx >= b->count) return false;
    int ring = (b->head - b->count + line_idx + MOD_LOG_LINES * 2) % MOD_LOG_LINES;
    strncpy(out, b->lines[ring], out_len - 1);
    out[out_len - 1] = '\0';
    return true;
}

void module_log_clear(const char *module_id)
{
    int idx = find_idx(module_id);
    if (idx < 0 || idx >= MAX_LOADED_MODULES) return;
    memset(&s_bufs[idx], 0, sizeof(mod_log_buf_t));
}
