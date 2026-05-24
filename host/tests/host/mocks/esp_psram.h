/* Mock: esp_psram.h */
#pragma once
#include <stddef.h>
#include <stdbool.h>
extern bool g_psram_initialized;
extern size_t g_psram_size;

static inline bool   esp_psram_is_initialized(void) { return g_psram_initialized; }
static inline size_t esp_psram_get_size(void)        { return g_psram_size; }
