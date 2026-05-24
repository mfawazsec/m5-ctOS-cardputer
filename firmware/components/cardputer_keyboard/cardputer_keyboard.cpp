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
#include <cstring>

/* ─── TCA8418 constants ─────────────────────────────────────────────────── */
#define TCA8418_ADDR   0x34
#define REG_CFG        0x01
#define REG_INT_ST     0x02
#define REG_EC         0x03
#define REG_FIFO       0x04

/* Cardputer keyboard matrix: 7 rows × 7 cols */
#define KBD_ROWS   7
#define KBD_COLS   7
#define KBD_NCODES (KBD_ROWS * KBD_COLS)

/* I2C frequency for TCA8418 (400 kHz) */
#define KBD_FREQ   400000

static const char *TAG = "kbd";

/* ─── Cardputer QWERTY keymap ───────────────────────────────────────────── */
/* Index = TCA8418 keycode - 1  (keycodes are 1-based, row-major).
 *
 *  Row 0 (codes  1– 7): `  1  2  3  4  5  6
 *  Row 1 (codes  8–14): q  w  e  r  t  y  u
 *  Row 2 (codes 15–21): a  s  d  f  g  h  i     ← 'i' = Fn+up arrow proxy
 *  Row 3 (codes 22–28): z  x  c  v  b  n  j     ← 'j' = unused / left?
 *  Row 4 (codes 29–35): Fn Spc k  l  ,  .  /    ← 'k'=Fn+down, code29=Fn
 *  Row 5 (codes 36–42): Ctrl Alt o  p  ;  '  Ent
 *  Row 6 (codes 43–49): Shift Del m  ←  ↓  ↑  →
 */
static const char s_keymap_normal[KBD_NCODES] = {
 /* row0 */ '`','1','2','3','4','5','6',
 /* row1 */ 'q','w','e','r','t','y','u',
 /* row2 */ 'a','s','d','f','g','h','i',
 /* row3 */ 'z','x','c','v','b','n','j',
 /* row4 */  0 ,' ','k','l',',','.','/',
 /* row5 */  0 , 0 ,'o','p',';','\'','\n',
 /* row6 */  0 ,127,'m', 0 , 0 , 0 , 0
};

static const char s_keymap_fn[KBD_NCODES] = {
 /* row0 */ '~','!','@','#','$','%','^',
 /* row1 */ 'Q','W','E','R','T','Y','U',
 /* row2 */ 'A','S','D','F','G','H','i',   /* FN+H → up  */
 /* row3 */ 'Z','X','C','V','B','N','j',
 /* row4 */  0 ,' ','k','l','<','>','?',   /* FN+K → left, FN+L → right */
 /* row5 */  0 , 0 ,'O','P',':','"','\n',
 /* row6 */  0 ,127,'M', 0 , 0 , 0 , 0
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

    /* Probe: read CFG register; if In_I2C isn't enabled or chip absent, skip */
    if (!M5.In_I2C.isEnabled()) {
        ESP_LOGW(TAG, "In_I2C not ready – keyboard disabled");
        return false;
    }

    /* Try to ping TCA8418 by reading CFG */
    uint8_t cfg = tca_read(REG_CFG);
    /* TCA8418 CFG reset value is 0x00; if we read 0xFF the chip isn't there */
    if (cfg == 0xFF) {
        ESP_LOGW(TAG, "TCA8418 not found at 0x%02X – keyboard disabled",
                 TCA8418_ADDR);
        return false;
    }

    /* Configure: enable key-event FIFO; 7×7 matrix */
    tca_write(REG_CFG, 0x01);          /* KE_IEN – key event interrupt enable */
    tca_write(0x1A, 0x7F);             /* KP_GPIO1: rows 0-6 → keypad */
    tca_write(0x1B, 0x7F);             /* KP_GPIO2: cols 0-6 → keypad */
    tca_write(0x1C, 0x00);

    /* Clear any stale events and interrupt flags */
    tca_write(REG_INT_ST, 0x1F);

    s_kbd.init = true;
    ESP_LOGI(TAG, "TCA8418 keyboard ready (cfg=0x%02X)", cfg);
    return true;
}

void cardputer_kb_update(void)
{
    if (!s_kbd.init) return;

    uint8_t ec = tca_read(REG_EC) & 0x0F;
    bool changed = false;

    while (ec--) {
        uint8_t ev = tca_read(REG_FIFO);
        bool press = (ev & 0x80) != 0;
        uint8_t code = ev & 0x7F;

        if (code == 0 || code > KBD_NCODES) continue;

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

    /* Fn key = code 29 (row 4, col 0) */
    s_kbd.fn_held = false;
    for (int i = 0; i < 6; i++) {
        if (s_kbd.pressed_codes[i] == 29) { s_kbd.fn_held = true; break; }
    }

    /* Rebuild key state */
    memset(&s_kbd.state, 0, sizeof(s_kbd.state));
    s_kbd.state.key.key.opt_key.fn = s_kbd.fn_held ? 1 : 0;

    const char *map = s_kbd.fn_held ? s_keymap_fn : s_keymap_normal;
    int out_idx = 0;
    for (int i = 0; i < 6 && out_idx < 6; i++) {
        uint8_t code = s_kbd.pressed_codes[i];
        if (code == 0 || code == 29) continue; /* skip no-key and Fn itself */
        char ch = (code - 1 < KBD_NCODES) ? map[code - 1] : 0;
        if (ch) s_kbd.state.key.key.key_data.keys[out_idx++] = (uint8_t)ch;
    }

    s_kbd.changed = changed;
}

bool cardputer_kb_is_change(void)  { return s_kbd.changed; }
bool cardputer_kb_is_pressed(void) { return s_kbd.pressed_count > 0; }
cardputer_kb_state_t cardputer_kb_get_state(void) { return s_kbd.state; }
