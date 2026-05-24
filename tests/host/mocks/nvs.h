/* Mock: nvs.h */
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY = 0, NVS_READWRITE } nvs_open_mode_t;

extern esp_err_t g_nvs_open_ret;

static inline esp_err_t nvs_open(const char *ns, nvs_open_mode_t m, nvs_handle_t *h) {
    (void)ns; (void)m;
    *h = 1;
    return g_nvs_open_ret;
}
static inline esp_err_t nvs_get_str(nvs_handle_t h, const char *k, char *out, size_t *len) {
    (void)h; (void)k; (void)out; (void)len;
    return ESP_ERR_NOT_FOUND; /* defaults always used */
}
static inline esp_err_t nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *v) {
    (void)h; (void)k; (void)v;
    return ESP_ERR_NOT_FOUND;
}
static inline esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *out, size_t *len) {
    (void)h; (void)k; (void)out; (void)len;
    return ESP_ERR_NOT_FOUND;
}
static inline esp_err_t nvs_set_str(nvs_handle_t h, const char *k, const char *v) {
    (void)h; (void)k; (void)v; return ESP_OK;
}
static inline esp_err_t nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v) {
    (void)h; (void)k; (void)v; return ESP_OK;
}
static inline esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *v, size_t l) {
    (void)h; (void)k; (void)v; (void)l; return ESP_OK;
}
static inline esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
static inline esp_err_t nvs_erase_all(nvs_handle_t h) { (void)h; return ESP_OK; }
