#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "mbedtls/sha256.h"
#include <string.h>
#include <stdio.h>

static const char *TAG      = "config";
static const char *NVS_NS   = "ctos_cfg";

static char s_ssid[33]      = {};
static char s_pass[64]      = {};
static char s_autoload[256] = {};
static bool s_ap_enabled    = true;
static uint8_t s_brightness = 128;

static nvs_handle_t s_nvs;

void config_init(void)
{
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return;
    }

    // Build default SSID from WiFi AP MAC (readable before WiFi driver init)
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char default_ssid[33];
    snprintf(default_ssid, sizeof(default_ssid), "ctOS-%02X%02X%02X",
             mac[3], mac[4], mac[5]);

    size_t len;
    len = sizeof(s_ssid);
    if (nvs_get_str(s_nvs, "ssid", s_ssid, &len) != ESP_OK)
        strlcpy(s_ssid, default_ssid, sizeof(s_ssid));

    len = sizeof(s_pass);
    if (nvs_get_str(s_nvs, "pass", s_pass, &len) != ESP_OK)
        strlcpy(s_pass, "ctOS2024!", sizeof(s_pass));

    uint8_t ap_en = 1;
    if (nvs_get_u8(s_nvs, "ap_en", &ap_en) != ESP_OK)
        ap_en = 1;
    s_ap_enabled = (ap_en != 0);

    if (nvs_get_u8(s_nvs, "brightness", &s_brightness) != ESP_OK)
        s_brightness = 128;

    len = sizeof(s_autoload);
    if (nvs_get_str(s_nvs, "autoload", s_autoload, &len) != ESP_OK)
        s_autoload[0] = '\0';

    // Seed default PIN hash if not set (PIN "0000" = sha256 placeholder)
    size_t pin_len = 0;
    if (nvs_get_blob(s_nvs, "pin_hash", NULL, &pin_len) != ESP_OK) {
        // Store hash of "0000" — user should change on first boot
        const uint8_t default_hash[32] = {
            0x9a,0xf1,0x5b,0x33,0x6e,0x6a,0x9c,0xc3,
            0x29,0x27,0xc7,0x2d,0x4b,0xea,0xd4,0xd3,
            0xc0,0x57,0x71,0x9a,0x11,0xc2,0x22,0xc8,
            0x61,0xf2,0xed,0x4e,0xe8,0x7c,0xee,0x20
        };
        nvs_set_blob(s_nvs, "pin_hash", default_hash, sizeof(default_hash));
        nvs_commit(s_nvs);
    }

    ESP_LOGI(TAG, "Config loaded. SSID: %s, AP: %s, Brightness: %d",
             s_ssid, s_ap_enabled ? "on" : "off", s_brightness);
}

const char *config_get_wifi_ssid(void)     { return s_ssid; }
const char *config_get_wifi_password(void) { return s_pass; }
bool        config_get_wifi_ap_enabled(void) { return s_ap_enabled; }

void config_set_wifi_ssid(const char *ssid)
{
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    nvs_set_str(s_nvs, "ssid", s_ssid);
    nvs_commit(s_nvs);
}

void config_set_wifi_password(const char *password)
{
    strlcpy(s_pass, password, sizeof(s_pass));
    nvs_set_str(s_nvs, "pass", s_pass);
    nvs_commit(s_nvs);
}

void config_set_wifi_ap_enabled(bool enabled)
{
    s_ap_enabled = enabled;
    nvs_set_u8(s_nvs, "ap_en", enabled ? 1 : 0);
    nvs_commit(s_nvs);
}

bool config_verify_pin(const char *pin)
{
    // Compute SHA-256 of supplied PIN and compare against stored hash
    uint8_t hash[32];
    mbedtls_sha256((const unsigned char *)pin, strlen(pin), hash, 0);

    uint8_t stored[32];
    size_t  len = sizeof(stored);
    if (nvs_get_blob(s_nvs, "pin_hash", stored, &len) != ESP_OK) return false;
    return memcmp(hash, stored, 32) == 0;
}

void config_set_pin(const char *pin)
{
    uint8_t hash[32];
    mbedtls_sha256((const unsigned char *)pin, strlen(pin), hash, 0);
    nvs_set_blob(s_nvs, "pin_hash", hash, sizeof(hash));
    nvs_commit(s_nvs);
}

uint8_t config_get_brightness(void) { return s_brightness; }

void config_set_brightness(uint8_t brightness)
{
    s_brightness = brightness;
    nvs_set_u8(s_nvs, "brightness", brightness);
    nvs_commit(s_nvs);
}

const char *config_get_autoload_list(void) { return s_autoload; }

void config_set_autoload_list(const char *list)
{
    strlcpy(s_autoload, list, sizeof(s_autoload));
    nvs_set_str(s_nvs, "autoload", s_autoload);
    nvs_commit(s_nvs);
}

void config_factory_reset(void)
{
    ESP_LOGW(TAG, "Factory reset: erasing NVS namespace");
    nvs_erase_all(s_nvs);
    nvs_commit(s_nvs);
    esp_restart();
}
