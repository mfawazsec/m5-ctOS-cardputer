#include "memory_view.h"
#include "modules/registry.h"
#include "wifi/hotspot.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_log.h"
#include "M5Unified.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "memory_view";

// Off-screen sprite to avoid flicker
static M5Canvas *s_canvas = nullptr;

void memory_view_init(void)
{
    s_canvas = new M5Canvas(&M5.Display);
    s_canvas->createSprite(M5.Display.width(), M5.Display.height());
    s_canvas->setTextSize(1);
    ESP_LOGI(TAG, "Memory view canvas ready");
}

void memory_view_render(void)
{
    if (!s_canvas) return;

    s_canvas->fillSprite(TFT_BLACK);
    s_canvas->setTextColor(TFT_GREEN, TFT_BLACK);
    s_canvas->setCursor(0, 0);

    // Header
    s_canvas->setTextColor(TFT_CYAN, TFT_BLACK);
    s_canvas->printf("m5-ctOS v2.0          [WiFi: %s]\n",
        hotspot_is_running() ? "AP" : "OFF");

    s_canvas->setTextColor(TFT_WHITE, TFT_BLACK);

    // Heap stats
    size_t free_heap  = esp_get_free_heap_size();
    size_t free_psram = esp_psram_is_initialized()
                        ? heap_caps_get_free_size(MALLOC_CAP_SPIRAM) : 0;
    s_canvas->printf("Free heap:  %4zu KB\n", free_heap / 1024);
    s_canvas->printf("PSRAM free: %4.1f MB\n", free_psram / (1024.0f * 1024.0f));

    // Module list
    int count = module_registry_count();
    s_canvas->setTextColor(TFT_YELLOW, TFT_BLACK);
    s_canvas->printf("Loaded modules (%d/%d):\n", count, MAX_LOADED_MODULES);

    s_canvas->setTextColor(TFT_WHITE, TFT_BLACK);
    for (int i = 0; i < count; i++) {
        module_info_t info;
        if (module_registry_get(i, &info) == ESP_OK) {
            s_canvas->setTextColor(info.running ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
            s_canvas->printf("  [%c] %-20s %s\n",
                info.running ? '*' : ' ',
                info.id,
                info.running ? "RUNNING" : "IDLE");
        }
    }

    // Key shortcuts footer
    s_canvas->setTextColor(TFT_DARKGREY, TFT_BLACK);
    s_canvas->setCursor(0, M5.Display.height() - 10);
    s_canvas->print("[M]odules [F]iles [S]ettings [W]iFi");

    s_canvas->pushSprite(0, 0);
}
