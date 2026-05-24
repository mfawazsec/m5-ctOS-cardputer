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

/* Cardputer keyboard matrix: 7 rows × 7 cols
 * TCA8418 key event codes always use a 10-column stride:
 *   code = row * 10 + col + 1  (regardless of how many cols are active)
 * So valid codes for our 7×7 matrix: 1-7, 11-17, 21-27, ..., 61-67 */
#define KBD_ROWS   7
#define KBD_COLS   7
#define KBD_STRIDE 10               /* TCA8418 fixed column stride in keycode formula */
#define KBD_NCODES (KBD_ROWS * KBD_STRIDE)  /* 70: covers all valid codes */

/* I2C frequency for TCA8418 (400 kHz) */
#define KBD_FREQ   400000

static const char *TAG = "kbd";

/* ─── Cardputer QWERTY keymap ───────────────────────────────────────────── */
/* Index = TCA8418 keycode - 1.
 * TCA8418 key code = row * 10 + col + 1 (fixed 10-col stride).
 * Each row occupies 10 slots; cols 7-9 are unused (0).
 *
 *  Row 0 (codes  1–10):  `  1  2  3  4  5  6  [0  0  0]
 *  Row 1 (codes 11–20):  q  w  e  r  t  y  u  [0  0  0]
 *  Row 2 (codes 21–30):  a  s  d  f  g  h  i  [0  0  0]
 *  Row 3 (codes 31–40):  z  x  c  v  b  n  j  [0  0  0]
 *  Row 4 (codes 41–50):  Fn Spc k  l  ,  .  / [0  0  0]  ← code41=Fn
 *  Row 5 (codes 51–60):  Ctrl Alt o  p  ;  '  Ent [0 0 0]
 *  Row 6 (codes 61–70):  Shift Del m  ←  ↓  ↑  → [0 0 0]
 */
static const char s_keymap_normal[KBD_NCODES] = {
 /* row0: 1-10  */ '`','1','2','3','4','5','6', 0, 0, 0,
 /* row1: 11-20 */ 'q','w','e','r','t','y','u', 0, 0, 0,
 /* row2: 21-30 */ 'a','s','d','f','g','h','i', 0, 0, 0,
 /* row3: 31-40 */ 'z','x','c','v','b','n','j', 0, 0, 0,
 /* row4: 41-50 */  0 ,' ','k','l',',','.','/', 0, 0, 0,
 /* row5: 51-60 */  0 , 0 ,'o','p',';','\'','\n',0,0, 0,
 /* row6: 61-70 */  0 ,127,'m', 0 , 0 , 0 , 0 , 0, 0, 0,
};

static const char s_keymap_fn[KBD_NCODES] = {
 /* row0: 1-10  */ '~','!','@','#','$','%','^', 0, 0, 0,
 /* row1: 11-20 */ 'Q','W','E','R','T','Y','U', 0, 0, 0,
 /* row2: 21-30 */ 'A','S','D','F','G','H','i', 0, 0, 0,  /* FN+H → up  */
 /* row3: 31-40 */ 'Z','X','C','V','B','N','j', 0, 0, 0,
 /* row4: 41-50 */  0 ,' ','k','l','<','>','?', 0, 0, 0,  /* FN+K/L = arrows */
 /* row5: 51-60 */  0 , 0 ,'O','P',':','"','\n',0, 0, 0,
 /* row6: 61-70 */  0 ,127,'M', 0 , 0 , 0 , 0 , 0, 0, 0,
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

    /* Configure: enable key-event FIFO; 7×7 matrix.
     * After reset all TCA8418 pins are GPIOs — we must write KP_GPIO
     * registers (0x1D-0x1F) to activate the keypad matrix.
     * KP_GPIO1 0x1D: bits[6:0] = rows R0-R6 → keypad
     * KP_GPIO2 0x1E: bits[7:2] = cols C0-C5 → keypad (R8/R9 stay GPIO)
     * KP_GPIO3 0x1F: bit[0]    = col  C6    → keypad */
    tca_write(REG_CFG, 0x01);          /* KE_IEN: key-event interrupt enable */
    tca_write(0x1D, 0x7F);             /* KP_GPIO1: R0-R6 as keypad rows */
    tca_write(0x1E, 0x7F);             /* KP_GPIO2: C0-C6 as keypad cols */
    tca_write(0x1F, 0x00);             /* KP_GPIO3: C8/C9 unused */

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

    /* Fn key = row 4, col 0 → code = 4*10 + 0 + 1 = 41 */
    s_kbd.fn_held = false;
    for (int i = 0; i < 6; i++) {
        if (s_kbd.pressed_codes[i] == 41) { s_kbd.fn_held = true; break; }
    }

    /* Rebuild key state */
    memset(&s_kbd.state, 0, sizeof(s_kbd.state));
    s_kbd.state.key.key.opt_key.fn = s_kbd.fn_held ? 1 : 0;

    const char *map = s_kbd.fn_held ? s_keymap_fn : s_keymap_normal;
    int out_idx = 0;
    for (int i = 0; i < 6 && out_idx < 6; i++) {
        uint8_t code = s_kbd.pressed_codes[i];
        if (code == 0 || code == 41) continue; /* skip no-key and Fn itself */
        char ch = (code - 1 < KBD_NCODES) ? map[code - 1] : 0;
        if (ch) s_kbd.state.key.key.key_data.keys[out_idx++] = (uint8_t)ch;
    }

    s_kbd.changed = changed;
}

bool cardputer_kb_is_change(void)  { return s_kbd.changed; }
bool cardputer_kb_is_pressed(void) { return s_kbd.pressed_count > 0; }
cardputer_kb_state_t cardputer_kb_get_state(void) { return s_kbd.state; }
