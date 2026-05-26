#pragma once
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOD_LOG_LINES    16
#define MOD_LOG_LINE_LEN 64

void module_log_push(const char *module_id, const char *line);
int  module_log_count(const char *module_id);
bool module_log_get(const char *module_id, int line_idx, char *out, size_t out_len);
void module_log_clear(const char *module_id);

#ifdef __cplusplus
}
#endif
