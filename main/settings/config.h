#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void config_init(void);

// WiFi
const char *config_get_wifi_ssid(void);
const char *config_get_wifi_password(void);
bool        config_get_wifi_ap_enabled(void);
void        config_set_wifi_ssid(const char *ssid);
void        config_set_wifi_password(const char *password);
void        config_set_wifi_ap_enabled(bool enabled);

// PIN (stored in NVS, never in source)
bool config_verify_pin(const char *pin);
void config_set_pin(const char *pin);

// Display
uint8_t config_get_brightness(void);
void    config_set_brightness(uint8_t brightness);

// Autoload — comma-separated module IDs
const char *config_get_autoload_list(void);
void        config_set_autoload_list(const char *list);

// System
void config_factory_reset(void);

#ifdef __cplusplus
}
#endif
