/* Mock: esp_heap_caps.h */
#pragma once
#include <stddef.h>
#include <stdlib.h>

#define MALLOC_CAP_SPIRAM   0x80
#define MALLOC_CAP_8BIT     0x04
#define MALLOC_CAP_DEFAULT  0x00

static inline void  *heap_caps_malloc(size_t s, uint32_t c) { (void)c; return malloc(s); }
static inline void   heap_caps_free(void *p) { free(p); }
static inline size_t heap_caps_get_free_size(uint32_t c) { (void)c; return 4*1024*1024; }
