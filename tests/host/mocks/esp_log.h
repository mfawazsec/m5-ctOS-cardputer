/* Mock: esp_log.h */
#pragma once
#include <stdio.h>
/* Capture log output for test assertions */
extern int g_log_count;
extern char g_last_log[512];

#define ESP_LOGI(tag, fmt, ...) do { \
    g_log_count++; \
    snprintf(g_last_log, sizeof(g_last_log), "[I][%s] " fmt, tag, ##__VA_ARGS__); \
    printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__); \
} while(0)

#define ESP_LOGW(tag, fmt, ...) do { \
    g_log_count++; \
    snprintf(g_last_log, sizeof(g_last_log), "[W][%s] " fmt, tag, ##__VA_ARGS__); \
    printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__); \
} while(0)

#define ESP_LOGE(tag, fmt, ...) do { \
    g_log_count++; \
    snprintf(g_last_log, sizeof(g_last_log), "[E][%s] " fmt, tag, ##__VA_ARGS__); \
    fprintf(stderr, "[E][%s] " fmt "\n", tag, ##__VA_ARGS__); \
} while(0)
