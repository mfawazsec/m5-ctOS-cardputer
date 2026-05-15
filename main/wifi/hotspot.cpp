#include "hotspot.h"
#include "settings/config.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "hotspot";
static bool s_running  = false;
static int  s_clients  = 0;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base != WIFI_EVENT) return;
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        s_clients++;
        ESP_LOGI(TAG, "Client connected (%d total)", s_clients);
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        if (s_clients > 0) s_clients--;
        ESP_LOGI(TAG, "Client disconnected (%d remaining)", s_clients);
    }
}

void hotspot_start(void)
{
    if (s_running) return;

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                wifi_event_handler, NULL);

    wifi_config_t ap_cfg = {};
    strlcpy((char *)ap_cfg.ap.ssid, config_get_wifi_ssid(),
            sizeof(ap_cfg.ap.ssid));
    strlcpy((char *)ap_cfg.ap.password, config_get_wifi_password(),
            sizeof(ap_cfg.ap.password));
    ap_cfg.ap.ssid_len      = strlen(config_get_wifi_ssid());
    ap_cfg.ap.channel       = 6;
    ap_cfg.ap.authmode      = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.beacon_interval = 100;

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    esp_wifi_start();

    s_running = true;
    s_clients = 0;
    ESP_LOGI(TAG, "AP started: SSID=%s on 192.168.4.1",
             config_get_wifi_ssid());
}

void hotspot_stop(void)
{
    if (!s_running) return;
    esp_wifi_stop();
    esp_wifi_deinit();
    s_running = false;
    s_clients = 0;
    ESP_LOGI(TAG, "AP stopped");
}

bool hotspot_is_running(void) { return s_running; }
int  hotspot_client_count(void) { return s_clients; }
