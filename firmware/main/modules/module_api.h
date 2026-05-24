#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Every module binary exports these symbols
typedef struct {
    const char *id;
    const char *name;
    const char *version;
    const char *author;
    const char *category;
} module_descriptor_t;

// IPC message types
typedef enum {
    CTOS_MSG_STOP = 0,
    CTOS_MSG_PAUSE,
    CTOS_MSG_RESUME,
    CTOS_MSG_STATUS_REQ,
    CTOS_MSG_LOG,
    CTOS_MSG_DISPLAY_UPDATE,
} ctos_msg_type_t;

typedef struct {
    ctos_msg_type_t type;
    uint8_t         data[128];
    size_t          data_len;
} ctos_msg_t;

// ctOS IPC API — passed to module on load
typedef struct {
    void     (*log)(const char *module_id, const char *msg);
    void     (*display_print)(const char *module_id, const char *line);
    void     (*display_clear)(const char *module_id);
    esp_err_t (*send_msg)(const char *module_id, ctos_msg_t *msg);
    void *    (*psram_alloc)(size_t size);
    void      (*psram_free)(void *ptr);
    uint32_t  (*get_free_psram)(void);
    uint32_t  (*get_free_heap)(void);
} ctos_api_t;

// Module entry point signature
typedef esp_err_t (*module_main_fn_t)(const ctos_api_t *api);

#ifdef __cplusplus
}
#endif
