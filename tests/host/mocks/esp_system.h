/* Mock: esp_system.h */
#pragma once
#include <stdint.h>
#include "esp_err.h"

extern uint32_t g_free_heap;
extern uint32_t g_min_free_heap;

static inline uint32_t esp_get_free_heap_size(void)         { return g_free_heap; }
static inline uint32_t esp_get_minimum_free_heap_size(void) { return g_min_free_heap; }
static inline void     esp_restart(void) { /* no-op in tests */ }
