/**
 * @file cardputer_keyboard.cpp
 * @brief Cardputer ADV keyboard driver (TCA8418 I2C keypad controller).
 *
 * Uses M5Unified's In_I2C so it shares the same I2C bus handle that M5GFX
 * already initialised — no separate i2c_driver_install needed.
 *
 * TCA8418 register map (relevant subset):
 *   0x01  CFG        – configuration register
 *   0x02  INT_STAT   – interrupt status
 *   0x03  KEY_LCK_EC – event count (bits[3:0])
 *   0x04  KEY_EVENT_A – FIFO (one byte per read)
 *
 * Key event byte: bit7 = press(1)/release(0), bits[6:0] = 1-based keycode.
 */

#include "cardputer_keyboard.h"
#include "M5Unified.h"     /* for M5.In_I2C */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <cstring>

/* ─── TCA8418 constants ─────────────────────────────────────────────────── */
#define TCA8418_ADDR   0x34
#define REG_CFG        0x01
#define REG_INT_ST     0x02
#define REG_EC         0x03
#define REG_FIFO       0x04

/* Cardputer ADV keyboard matrix: 7 TCA8418 rows × 8 TCA8418 cols.
 * TCA8418 key event codes use a fixed 10-column stride:
 *   code = tca_row * 10 + tca_col + 1
 * Valid codes: 1-8, 11-18, 21-28, 31-38, 41-48, 51-58, 61-68.
 *
 * Physical layout is 4 rows × 14 cols, interleaved via:
 *   phys_row = tca_col & 3
 *   phys_col = tca_row * 2 + (tca_col >> 2)
 * Keymaps below are indexed by (code - 1) with stride 10. */
#define KBD_ROWS   7
#define KBD_COLS   8               /* 8 TCA8418 columns */
#define KBD_STRIDE 10              /* TCA8418 fixed column stride in keycode formula */
#define KBD_NCODES (KBD_ROWS * KBD_STRIDE)  /* 70: covers all valid codes */

/* Special key codes (not yielding printable characters) */
#define CODE_FN    3   /* TCA row0, col2 */
#define CODE_SHIFT 7   /* TCA row0, col6 */
#define CODE_CTRL  4   /* TCA row0, col3 */
#define CODE_ALT   14  /* TCA row1, col3 */
#define CODE_OPT   8   /* TCA row0, col7 */

/* I2C frequency for TCA8418 (400 kHz) */
#define KBD_FREQ   400000

static const char *TAG = "kbd";

/* ─── Cardputer ADV QWERTY keymap ───────────────────────────────────────── */
/* Index = keycode - 1.  Each TCA8418 row occupies 10 slots; cols 8-9 unused.
 *
 * Physical layout (4 rows × 14 cols) mapped via:
 *   phys_row = tca_col & 3,  phys_col = tca_row*2 + (tca_col >> 2)
 *
 * TCA row 0 (codes  1-10): `   Tab  Fn  Ctrl 1   q  Shift Opt  -   -
 * TCA row 1 (codes 11-20): 2   w   a   Alt  3   e   s   z   -   -
 * TCA row 2 (codes 21-30): 4   r   d   x    5   t   f   c   -   -
 * TCA row 3 (codes 31-40): 6   y   g   v    7   u   h   b   -   -
 * TCA row 4 (codes 41-50): 8   i   j   n    9   o   k   m   -   -
 * TCA row 5 (codes 51-60): 0   p   l   ,    -   [   ;   .   -   -
 * TCA row 6 (codes 61-70): =   ]   '   /   DEL  \  Ent Spc  -   -
 */
static const char s_keymap_normal[KBD_NCODES] = {
 /* codes  1-10 */ '`', '\t',  0 ,  0 , '1', 'q',  0 ,  0 ,  0 ,  0 ,
 /* codes 11-20 */ '2', 'w', 'a',  0 , '3', 'e', 's', 'z',  0 ,  0 ,
 /* codes 21-30 */ '4', 'r', 'd', 'x', '5', 't', 'f', 'c',  0 ,  0 ,
 /* codes 31-40 */ '6', 'y', 'g', 'v', '7', 'u', 'h', 'b',  0 ,  0 ,
 /* codes 41-50 */ '8', 'i', 'j', 'n', '9', 'o', 'k', 'm',  0 ,  0 ,
 /* codes 51-60 */ '0', 'p', 'l', ',', '-', '[', ';', '.',  0 ,  0 ,
 /* codes 61-70 */ '=', ']','\'', '/',127 ,'\\','\n', ' ',  0 ,  0 ,
};

/* Fn / Shift layer */
static const char s_keymap_fn[KBD_NCODES] = {
 /* codes  1-10 */ '~', '\t',  0 ,  0 , '!', 'Q',  0 ,  0 ,  0 ,  0 ,
 /* codes 11-20 */ '@', 'W', 'A',  0 , '#', 'E', 'S', 'Z',  0 ,  0 ,
 /* codes 21-30 */ '$', 'R', 'D', 'X', '%', 'T', 'F', 'C',  0 ,  0 ,
 /* codes 31-40 */ '^', 'Y', 'G', 'V', '&', 'U', 'H', 'B',  0 ,  0 ,
 /* codes 41-50 */ '*', 'I', 'J', 'N', '(', 'O', 'K', 'M',  0 ,  0 ,
 /* codes 51-60 */ ')', 'P', 'L', '<', '_', '{', ':', '>',  0 ,  0 ,
 /* codes 61-70 */ '+', '}', '"', '?',127 , '|','\n', ' ',  0 ,  0 ,
};

/* ─── driver state ──────────────────────────────────────────────────────── */
static struct {
    bool     init;
    bool     changed;
    uint8_t  pressed_count;
    uint8_t  pressed_codes[6];   /* raw TCA8418 keycodes currently held */
    bool     fn_held;
    cardputer_kb_state_t state;
} s_kbd;

static QueueHandle_t s_inject_queue = nullptr;

/* Define the global C++ wrapper instance */
CardputerKeyboard CardputerKb;

/* ─── I2C helpers via M5.In_I2C ─────────────────────────────────────────── */
static inline bool tca_write(uint8_t reg, uint8_t val)
{
    return M5.In_I2C.writeRegister8(TCA8418_ADDR, reg, val, KBD_FREQ);
}

static inline uint8_t tca_read(uint8_t reg)
{
    return M5.In_I2C.readRegister8(TCA8418_ADDR, reg, KBD_FREQ);
}

/* ─── public API ─────────────────────────────────────────────────────────── */

bool cardputer_kb_init(void)
{
    memset(&s_kbd, 0, sizeof(s_kbd));
    if (!s_inject_queue)
        s_inject_queue = xQueueCreate(8, sizeof(char));

    if (!M5.In_I2C.isEnabled()) {
        ESP_LOGW(TAG, "In_I2C not ready – keyboard disabled");
        return false;
    }

    /* I2C bus scan — readRegister returns true only on ACK */
    ESP_LOGI(TAG, "I2C scan on In_I2C bus:");
    uint8_t dummy;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (M5.In_I2C.readRegister(addr, 0x00, &dummy, 1, KBD_FREQ)) {
            ESP_LOGI(TAG, "  0x%02X", addr);
        }
    }

    /* Try to ping TCA8418 by reading CFG */
    uint8_t cfg = tca_read(REG_CFG);
    if (cfg == 0xFF) {
        ESP_LOGW(TAG, "TCA8418 not found at 0x%02X – keyboard disabled",
                 TCA8418_ADDR);
        return false;
    }

    tca_write(REG_CFG, 0x01);          /* KE_IEN: key-event interrupt enable */
    tca_write(0x1D, 0x7F);             /* KP_GPIO1: rows 0-6 as keypad (7 rows) */
    tca_write(0x1E, 0xFF);             /* KP_GPIO2: cols 0-7 as keypad (8 cols) */
    tca_write(0x1F, 0x00);             /* KP_GPIO3: no extra cols */

    /* Clear any stale events and interrupt flags */
    tca_write(REG_INT_ST, 0x1F);

    ESP_LOGI(TAG, "TCA8418 init: cfg=0x%02X KP_GPIO=0x%02X/0x%02X/0x%02X INT=0x%02X EC=0x%02X",
             cfg,
             tca_read(0x1D), tca_read(0x1E), tca_read(0x1F),
             tca_read(REG_INT_ST), tca_read(REG_EC));

    s_kbd.init = true;
    ESP_LOGI(TAG, "TCA8418 keyboard ready — press a key now");
    return true;
}

void cardputer_kb_update(void)
{
    if (!s_kbd.init) return;

    uint8_t ec = tca_read(REG_EC) & 0x0F;
    bool changed = false;

    if (ec) {
        ESP_LOGI(TAG, "FIFO events: %d (INT=0x%02X)", ec, tca_read(REG_INT_ST));
    }

    while (ec--) {
        uint8_t ev = tca_read(REG_FIFO);
        bool press = (ev & 0x80) != 0;
        uint8_t code = ev & 0x7F;

        ESP_LOGI(TAG, "  ev=0x%02X %s code=%d", ev, press ? "PRESS" : "RELEASE", code);
        if (press && code != 0 && code <= KBD_NCODES)
            ESP_LOGI(TAG, "KEY_TEST_RAW code=%u", (unsigned)code);

        if (code == 0 || code > KBD_NCODES) { ESP_LOGW(TAG, "  → discarded"); continue; }

        if (press) {
            bool found = false;
            for (int i = 0; i < 6; i++) {
                if (s_kbd.pressed_codes[i] == code) { found = true; break; }
            }
            if (!found) {
                for (int i = 0; i < 6; i++) {
                    if (s_kbd.pressed_codes[i] == 0) {
                        s_kbd.pressed_codes[i] = code;
                        s_kbd.pressed_count++;
                        break;
                    }
                }
            }
        } else {
            for (int i = 0; i < 6; i++) {
                if (s_kbd.pressed_codes[i] == code) {
                    s_kbd.pressed_codes[i] = 0;
                    if (s_kbd.pressed_count > 0) s_kbd.pressed_count--;
                    break;
                }
            }
        }
        changed = true;
    }

    /* Clear interrupt flags */
    tca_write(REG_INT_ST, 0x1F);

    /* Fn (code 3) or Shift (code 7) activates the fn/shift layer */
    s_kbd.fn_held = false;
    for (int i = 0; i < 6; i++) {
        uint8_t c = s_kbd.pressed_codes[i];
        if (c == CODE_FN || c == CODE_SHIFT) { s_kbd.fn_held = true; break; }
    }

    /* Rebuild key state */
    memset(&s_kbd.state, 0, sizeof(s_kbd.state));
    s_kbd.state.key.key.opt_key.fn = s_kbd.fn_held ? 1 : 0;

    const char *map = s_kbd.fn_held ? s_keymap_fn : s_keymap_normal;
    int out_idx = 0;
    for (int i = 0; i < 6 && out_idx < 6; i++) {
        uint8_t code = s_kbd.pressed_codes[i];
        /* skip modifier keys and empty slots */
        if (code == 0 || code == CODE_FN || code == CODE_SHIFT ||
            code == CODE_CTRL || code == CODE_ALT || code == CODE_OPT) continue;
        char ch = (code - 1 < KBD_NCODES) ? map[code - 1] : 0;
        if (ch) {
            ESP_LOGI(TAG, "  → char '%c' (0x%02X)", ch >= 32 && ch < 127 ? ch : '?', (uint8_t)ch);
            ESP_LOGI(TAG, "KEY_TEST_CHAR code=%u char=%u", (unsigned)code, (unsigned)(uint8_t)ch);
            s_kbd.state.key.key.key_data.keys[out_idx++] = (uint8_t)ch;
        }
    }

    s_kbd.changed = changed;

    /* Virtual key injection — overrides state only when no real key is held */
    if (s_inject_queue && s_kbd.pressed_count == 0) {
        char ch = 0;
        if (xQueueReceive(s_inject_queue, &ch, 0) == pdTRUE) {
            s_kbd.state.key.key.key_data.keys[0] = (uint8_t)ch;
            s_kbd.changed      = true;
            s_kbd.pressed_count = 1;
        }
    }
}

void cardputer_kb_inject_char(char ch)
{
    if (s_inject_queue)
        xQueueSend(s_inject_queue, &ch, 0);
}

bool cardputer_kb_is_change(void)  { return s_kbd.changed; }
bool cardputer_kb_is_pressed(void) { return s_kbd.pressed_count > 0; }
cardputer_kb_state_t cardputer_kb_get_state(void) { return s_kbd.state; }
