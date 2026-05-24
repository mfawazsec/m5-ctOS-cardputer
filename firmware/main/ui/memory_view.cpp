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

// Off-screen sprite — only used if createSprite succeeds (needs ~63KB contiguous heap).
// Falls back to direct display drawing if allocation fails.
static M5Canvas *s_canvas = nullptr;
static bool      s_sprite_ok = false;

/* textSize 1.5 → 9×12 px per glyph; screen 240×135 → 26 cols, 11 rows */
static constexpr float TS  = 1.5f;
static constexpr int LINEH = 12;

void memory_view_init(void)
{
    s_canvas = new M5Canvas(&M5.Display);
    // Try 8-bit depth first (~32KB contiguous) to avoid heap fragmentation.
    // Full 16-bit sprite needs ~63KB which rarely fits after boot.
    s_canvas->setColorDepth(8);
    void *buf = s_canvas->createSprite(M5.Display.width(), M5.Display.height());
    if (!buf) {
        // 8-bit also failed — fall back to direct display rendering
        s_canvas->setColorDepth(16);
    }
    s_sprite_ok = (buf != nullptr);
    s_canvas->setTextSize(TS);
    ESP_LOGI(TAG, "Memory view canvas ready (sprite=%s, heap_free=%u)",
             s_sprite_ok ? "8bpp-OK" : "FAIL-direct",
             (unsigned)esp_get_free_heap_size());
}

static void render_to(LovyanGFX *dst)
{
    dst->setTextSize(TS);
    int y = 0;

    /* ── Header ── */
    dst->fillScreen(TFT_BLACK);
    dst->setTextColor(TFT_CYAN, TFT_BLACK);
    dst->setCursor(0, y);
    dst->printf("ctOS v2.0  WiFi:%s", hotspot_is_running() ? "AP " : "OFF");
    y += LINEH;

    /* ── Heap stats ── */
    dst->setTextColor(TFT_WHITE, TFT_BLACK);
    size_t free_heap  = esp_get_free_heap_size();
    size_t free_psram = esp_psram_is_initialized()
                        ? heap_caps_get_free_size(MALLOC_CAP_SPIRAM) : 0;
    dst->setCursor(0, y);
    dst->printf("Heap:%zuK PSRAM:%zuK", free_heap / 1024, free_psram / 1024);
    y += LINEH;

    /* ── Module list ── */
    int count = module_registry_count();
    dst->setTextColor(TFT_YELLOW, TFT_BLACK);
    dst->setCursor(0, y);
    dst->printf("Modules (%d/%d):", count, MAX_LOADED_MODULES);
    y += LINEH;

    int limit = M5.Display.height() - LINEH * 2;
    for (int i = 0; i < count && y < limit; i++) {
        module_info_t info;
        if (module_registry_get(i, &info) == ESP_OK) {
            dst->setTextColor(info.running ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
            dst->setCursor(0, y);
            dst->printf("[%c] %-16s", info.running ? '*' : ' ', info.id);
            y += LINEH;
        }
    }

    /* ── Footer shortcuts ── */
    dst->setTextColor(TFT_DARKGREY, TFT_BLACK);
    dst->setCursor(0, M5.Display.height() - LINEH);
    dst->print("[M]ods [F]iles [S]et [W]ifi");
}

void memory_view_render(void)
{
    if (s_sprite_ok && s_canvas) {
        s_canvas->fillSprite(TFT_BLACK);
        render_to(s_canvas);
        s_canvas->pushSprite(0, 0);
    } else {
        M5.Display.startWrite();
        render_to(&M5.Display);
        M5.Display.endWrite();
    }
}
