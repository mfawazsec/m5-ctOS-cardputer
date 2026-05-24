/* Mock: esp_vfs_fat.h */
#pragma once
#include "esp_err.h"
typedef struct {} esp_vfs_fat_mount_config_t;
static inline esp_err_t esp_vfs_fat_spiflash_mount_rw_wl(
    const char *base, const char *partition, const void *cfg, void **handle) {
    (void)base; (void)partition; (void)cfg; (void)handle;
    return ESP_OK;
}
