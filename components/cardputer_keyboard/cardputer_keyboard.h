/**
 * @file cardputer_keyboard.h
 * @brief Cardputer ADV keyboard driver (TCA8418 I2C keypad controller, addr 0x34).
 *
 * Exposes a minimal "M5.Keyboard"-compatible API so the rest of ctOS can call
 *   kb.isChange()           – true if key state changed since last update()
 *   kb.isPressed()          – true if at least one key is currently held
 *   kb.getState()           – returns cardputer_kb_state_t
 *   state.key.key_data.keys[0]  – first pressed ASCII char (0 = none)
 *   state.key.opt_key.fn        – non-zero when Fn modifier is held
 *
 * Call cardputer_kb_init() once at startup, then cardputer_kb_update() every
 * loop iteration before reading state.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── raw key-data layout (mirrors old M5Cardputer API) ── */
typedef union {
    struct {
        uint8_t keys[6];   /**< up to 6 simultaneous key ASCII codes */
        uint8_t _reserved[2];
    } key_data;
    struct {
        uint8_t fn  : 1;   /**< Fn modifier pressed */
        uint8_t tab : 1;
        uint8_t ctrl: 1;
        uint8_t alt : 1;
        uint8_t _pad: 4;
    } opt_key;
    uint64_t raw;
} cardputer_key_t;

typedef struct {
    struct {
        cardputer_key_t key;
    } key;
} cardputer_kb_state_t;

/* ── driver functions ── */

/**
 * Initialise the TCA8418 over the internal I2C bus.
 * Must be called after M5.begin() (which starts In_I2C).
 * Returns true on success, false if the chip is not found.
 */
bool cardputer_kb_init(void);

/**
 * Poll the TCA8418 event FIFO.  Call once per main-loop iteration.
 */
void cardputer_kb_update(void);

/** Returns true if the key state has changed since the last update(). */
bool cardputer_kb_is_change(void);

/** Returns true if at least one key is currently pressed. */
bool cardputer_kb_is_pressed(void);

/** Returns a snapshot of the current key state. */
cardputer_kb_state_t cardputer_kb_get_state(void);

#ifdef __cplusplus
}
#endif

/* ── C++ convenience wrapper ── */
#ifdef __cplusplus

class CardputerKeyboard {
public:
    bool init(void)         { return cardputer_kb_init(); }
    void update(void)       { cardputer_kb_update(); }
    bool isChange(void)     { return cardputer_kb_is_change(); }
    bool isPressed(void)    { return cardputer_kb_is_pressed(); }
    cardputer_kb_state_t getState(void) { return cardputer_kb_get_state(); }
};

extern CardputerKeyboard CardputerKb;

#endif /* __cplusplus */
