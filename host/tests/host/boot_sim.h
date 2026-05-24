/**
 * boot_sim.h — Host-side simulation API for ctOS boot phases.
 *
 * Each boot_phase_*() function wraps the real logic extracted from
 * ctOS_main.cpp and the subsystem modules, but uses mock ESP-IDF
 * headers so it can be compiled and run on Linux with gcc.
 */
#pragma once
#include <stdbool.h>

/* ── Per-phase functions ─────────────────────────────────────────────── */

/** [1/9] NVS flash init. Returns 0 on success. */
int  boot_phase_nvs(void);

/** [2/9] PSRAM detection. Always succeeds (logs only). */
void boot_phase_psram(void);

/** [3/9] Config init from NVS. */
void boot_phase_config(void);

/** [4/9] Module registry init. */
void boot_phase_registry(void);

/** [5/9] Module loader init. */
void boot_phase_loader(void);

/**
 * [6/9] Keyboard init.
 * @param force_fail  If true, always returns false (simulates missing chip).
 * @return true if keyboard OK, false if not found.
 */
bool boot_phase_keyboard(bool force_fail);

/**
 * [8/9] WiFi / hotspot start.
 * @return number of hotspot_start() calls made (0 if AP disabled).
 */
int  boot_phase_wifi(void);

/**
 * [9/9] Autoload modules from comma-separated list.
 * @param list  e.g. "wifi_scan,ble_hid" or "" for none.
 */
void boot_phase_autoload(const char *list);

/* ── Integration helpers ─────────────────────────────────────────────── */

/** Run the complete 9-phase sequence. Returns 0 on success. */
int boot_run_full_sequence(void);

/* ── Registry helpers (exposed for tests) ───────────────────────────── */

/** Add a module to the registry. Returns ESP_OK or error. */
int boot_registry_add(const char *id, const char *version);

/** Get module count. */
int boot_get_module_count(void);

/** Get module at index. Returns ESP_OK or error. */
int boot_registry_get(int index, void *out);

/** Start a module task. Returns ESP_OK or error. */
int boot_loader_start(const char *id);

/* ── Config accessors (exposed for tests) ───────────────────────────── */
const char *boot_get_ssid(void);
const char *boot_get_password(void);
bool        boot_get_ap_enabled(void);
void        boot_set_ap_enabled(bool en);
