#pragma once
#include "M5Unified.h"
#include "cardputer_keyboard.h"
#include "modules/module_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MOD_LH 12  // line height in pixels

static inline void mod_drain_keys(void) {
    do { vTaskDelay(pdMS_TO_TICKS(50)); CardputerKb.update(); }
    while (CardputerKb.isPressed());
}

void mod_show_log_view(const char *module_id, const char *title);
void ble_nimble_ensure_started(void);
