#include "memory_view.h"
#include "modules/registry.h"
#include "wifi/hotspot.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "M5Unified.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "memory_view";

static M5Canvas *s_canvas  = nullptr;
static bool      s_sprite_ok = false;

/* textSize 1.5 → each glyph is 9×12 px; 240px wide → 26 chars per line */
static constexpr float TS    = 1.5f;
static constexpr int   LINEH = 12;
static constexpr int   COLS  = 26;   /* chars that fill one line exactly */

void memory_view_init(void)
{
    s_canvas = new M5Canvas(&M5.Display);
    // 8-bit sprite needs ~32KB contiguous; try it before falling back.
    s_canvas->setColorDepth(8);
    void *buf = s_canvas->createSprite(M5.Display.width(), M5.Display.height());
    if (!buf) {
        s_canvas->setColorDepth(16); // reset depth; sprite stays empty
    }
    s_sprite_ok = (buf != nullptr);
    s_canvas->setTextSize(TS);
    ESP_LOGI(TAG, "Memory view init (sprite=%s, heap_free=%u, largest_block=%u)",
             s_sprite_ok ? "8bpp" : "direct",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

/* ── helpers ────────────────────────────────────────────────────────────── */

// Printf a line padded to exactly COLS chars so the background fill
// completely overwrites whatever was there before — no fillScreen needed.
static void line_printf(LovyanGFX *dst, int y,
                        uint16_t fg, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

static void line_printf(LovyanGFX *dst, int y,
                        uint16_t fg, const char *fmt, ...)
{
    char tmp[COLS + 1];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    // Pad (or truncate) to exactly COLS characters
    int len = strlen(tmp);
    if (len < COLS) memset(tmp + len, ' ', COLS - len);
    tmp[COLS] = '\0';

    dst->setTextColor(fg, TFT_BLACK);
    dst->setCursor(0, y);
    dst->print(tmp);
}

/* ── render ────────────────────────────────────────────────────────────── */

static void render_to(LovyanGFX *dst, bool first_frame)
{
    dst->setTextSize(TS);

    if (first_frame) {
        dst->fillScreen(TFT_BLACK);  // one-time clear on first draw only
    }

    int y = 0;

    /* Header */
    line_printf(dst, y, TFT_CYAN,
                "ctOS v2.0  WiFi:%s",
                hotspot_is_running() ? "AP" : "OFF");
    y += LINEH;

    /* Heap stats */
    line_printf(dst, y, TFT_WHITE,
                "Heap:%uK PSRAM:%uK",
                (unsigned)(esp_get_free_heap_size() / 1024),
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    y += LINEH;

    /* Module list header */
    int count = module_registry_count();
    line_printf(dst, y, TFT_YELLOW, "Modules (%d/%d):", count, MAX_LOADED_MODULES);
    y += LINEH;

    int limit = M5.Display.height() - LINEH * 2;
    for (int i = 0; i < count && y < limit; i++) {
        module_info_t info;
        if (module_registry_get(i, &info) == ESP_OK) {
            line_printf(dst, y,
                        info.running ? TFT_GREEN : TFT_DARKGREY,
                        "[%c] %s",
                        info.running ? '*' : ' ',
                        info.id);
            y += LINEH;
        }
    }

    /* Footer */
    line_printf(dst, M5.Display.height() - LINEH, TFT_DARKGREY,
                "[M]ods [F]iles [S]et [W]ifi");
}

void memory_view_render(void)
{
    static bool s_first = true;

    if (s_sprite_ok && s_canvas) {
        if (s_first) s_canvas->fillSprite(TFT_BLACK);
        render_to(s_canvas, s_first);
        s_canvas->pushSprite(0, 0);
    } else {
        M5.Display.startWrite();
        render_to(&M5.Display, s_first);
        M5.Display.endWrite();
    }
    s_first = false;
}
