/**
 * boot_banner.cpp — ctOS boot splash screen
 *
 * Resource budget:
 *   Flash  : ~300 bytes (.rodata string literals)
 *   RAM    : 0 bytes heap — all locals on stack
 *   Time   : ~1.5 s visible, then fades to menu
 *
 * Renders the ASCII-art logo from the README directly onto M5.Display.
 * The Cardputer ADV screen is 240×135 px; at textSize 1 each glyph is
 * 6×8 px, giving plenty of room for the 25-char-wide art block.
 *
 * Colour palette (565 RGB, all compile-time constants):
 *   Art         bright green  #00FF41   → 0x07E8
 *   Subtitle    cyan          #00E5FF   → 0x072F  (approximately TFT_CYAN)
 *   Version     dim grey      #888888   → 0x8C51
 *   Hint        dark grey     #444444   → 0x4208
 */

#include "boot_banner.h"
#include "M5Unified.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ── Colour constants (RGB565, evaluated at compile time) ─────────────────── */
static constexpr uint16_t COL_ART      = 0x07E8u; /* #00FF41  bright green  */
static constexpr uint16_t COL_SUBTITLE = 0x07FFu; /* #00FFFF  cyan          */
static constexpr uint16_t COL_VERSION  = 0x8C51u; /* #888888  dim grey      */
static constexpr uint16_t COL_HINT     = 0x4208u; /* #444444  dark grey     */
static constexpr uint16_t COL_BG       = 0x0000u; /* #000000  black         */

/* ── ASCII art — stored in flash (.rodata), never copied to RAM ───────────── */
static const char * const ART[] = {
    "  ____ _____ ___  ____ ",
    " / ___|_   _/ _ \\/ ___|",
    "| |     | || | | \\___ \\",
    "| |___  | || |_| |___) |",
    " \\____| |_| \\___/|____/ ",
};
static constexpr int ART_LINES = sizeof(ART) / sizeof(ART[0]);

static const char SUBTITLE[] = "ESP32-S3  \xb7  M5Stack Cardputer ADV";
static const char VERSION[]  = "v2.0";
static const char HINT[]     = "[ press any key ]";

/* ── Helper: draw a centred string at given y ─────────────────────────────── */
static void draw_centred(const char *str, int y, uint16_t color)
{
    auto &d  = M5.Display;
    /* Each glyph is textSize * 6 px wide at the built-in font */
    int text_w = (int)strlen(str) * 6; /* textSize is always 1 here */
    int x      = (d.width() - text_w) / 2;
    if (x < 0) x = 0;
    d.setTextColor(color, COL_BG); /* explicit bg prevents ghost pixels */
    d.setCursor(x, y);
    d.print(str);
}

/* ── Public entry point ───────────────────────────────────────────────────── */
void boot_banner_show(void)
{
    auto &d = M5.Display;

    d.fillScreen(COL_BG);
    d.setTextSize(1); /* 6×8 px per glyph — no heap sprite needed */

    /* Layout (all heights in pixels at textSize 1):
     *   art block   : ART_LINES × 8 = 40 px
     *   gap         : 4 px
     *   subtitle    : 8 px
     *   gap         : 4 px
     *   version     : 8 px
     *   gap         : 6 px
     *   hint        : 8 px
     *   ─────────────────
     *   total       : 78 px  (fits comfortably in 135 px height)
     */
    static constexpr int LINE_H  = 8;
    static constexpr int TOTAL_H = ART_LINES * LINE_H + 4 + LINE_H + 4 + LINE_H + 6 + LINE_H;
    int y = (d.height() - TOTAL_H) / 2; /* vertically centred */

    /* ── ASCII art in bright green ────────────────────────────────────────── */
    for (int i = 0; i < ART_LINES; i++) {
        draw_centred(ART[i], y, COL_ART);
        y += LINE_H;
    }

    /* ── Subtitle ─────────────────────────────────────────────────────────── */
    y += 4;
    draw_centred(SUBTITLE, y, COL_SUBTITLE);
    y += LINE_H;

    /* ── Version ──────────────────────────────────────────────────────────── */
    y += 4;
    draw_centred(VERSION, y, COL_VERSION);
    y += LINE_H;

    /* ── Key-press hint ───────────────────────────────────────────────────── */
    y += 6;
    draw_centred(HINT, y, COL_HINT);

    /* Visible for ~1.5 s — short enough not to feel sluggish,
     * long enough to read the logo.  Yield to RTOS rather than
     * busy-waiting so other tasks (esp. watchdog) keep running. */
    vTaskDelay(pdMS_TO_TICKS(1500));
}
