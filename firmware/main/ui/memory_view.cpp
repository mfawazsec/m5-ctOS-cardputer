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

/* textSize 1.5 → 9×12 px per glyph; screen 240×135 → 26 cols, 11 rows */
static constexpr float TS  = 1.5f;
static constexpr int LINEH = 12;

void memory_view_init(void)
{
    s_canvas = new M5Canvas(&M5.Display);
    s_canvas->createSprite(M5.Display.width(), M5.Display.height());
    s_canvas->setTextSize(TS);
    ESP_LOGI(TAG, "Memory view canvas ready");
}

void memory_view_render(void)
{
    if (!s_canvas) return;

    s_canvas->fillSprite(TFT_BLACK);
    s_canvas->setTextSize(TS);

    int y = 0;

    /* ── Header ── */
    s_canvas->setTextColor(TFT_CYAN, TFT_BLACK);
    s_canvas->setCursor(0, y);
    s_canvas->printf("ctOS v2.0  WiFi:%s",
        hotspot_is_running() ? "AP " : "OFF");
    y += LINEH;

    /* ── Heap stats ── */
    s_canvas->setTextColor(TFT_WHITE, TFT_BLACK);
    size_t free_heap  = esp_get_free_heap_size();
    size_t free_psram = esp_psram_is_initialized()
                        ? heap_caps_get_free_size(MALLOC_CAP_SPIRAM) : 0;
    s_canvas->setCursor(0, y);
    s_canvas->printf("Heap:%zuK PSRAM:%zuK", free_heap / 1024, free_psram / 1024);
    y += LINEH;

    /* ── Module list ── */
    int count = module_registry_count();
    s_canvas->setTextColor(TFT_YELLOW, TFT_BLACK);
    s_canvas->setCursor(0, y);
    s_canvas->printf("Modules (%d/%d):", count, MAX_LOADED_MODULES);
    y += LINEH;

    for (int i = 0; i < count && y < M5.Display.height() - LINEH * 2; i++) {
        module_info_t info;
        if (module_registry_get(i, &info) == ESP_OK) {
            s_canvas->setTextColor(info.running ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
            s_canvas->setCursor(0, y);
            s_canvas->printf("[%c] %-16s", info.running ? '*' : ' ', info.id);
            y += LINEH;
        }
    }

    /* ── Footer shortcuts ── */
    s_canvas->setTextColor(TFT_DARKGREY, TFT_BLACK);
    s_canvas->setCursor(0, M5.Display.height() - LINEH);
    s_canvas->print("[M]ods [F]iles [S]et [W]ifi");

    s_canvas->pushSprite(0, 0);
}
