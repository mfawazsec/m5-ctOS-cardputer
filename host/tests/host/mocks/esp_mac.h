/* Mock: esp_mac.h */
#pragma once
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    ESP_MAC_WIFI_STA = 0,
    ESP_MAC_WIFI_SOFTAP,
    ESP_MAC_BT,
    ESP_MAC_ETH,
} esp_mac_type_t;

static inline esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type) {
    (void)type;
    /* Deterministic fake MAC for tests */
    mac[0] = 0xDE; mac[1] = 0xAD;
    mac[2] = 0xBE; mac[3] = 0xEF;
    mac[4] = 0x12; mac[5] = 0x34;
    return ESP_OK;
}
static inline esp_err_t esp_base_mac_addr_get(uint8_t *mac) {
    return esp_read_mac(mac, ESP_MAC_WIFI_STA);
}
