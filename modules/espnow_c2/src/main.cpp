#include "module_api.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "espnow_c2";
static const ctos_api_t *s_api = nullptr;

static uint8_t         s_peer_mac[6]   = {};
static bool            s_peer_set      = false;
static QueueHandle_t   s_rx_queue;

typedef struct {
    uint8_t src_mac[6];
    uint8_t data[250];
    int     data_len;
    int     rssi;
} rx_msg_t;

static void espnow_rx_cb(const esp_now_recv_info_t *info,
                          const uint8_t *data, int data_len)
{
    if (data_len <= 0 || data_len > 250) return;
    rx_msg_t msg;
    memcpy(msg.src_mac, info->src_addr, 6);
    memcpy(msg.data, data, data_len);
    msg.data_len = data_len;
    msg.rssi     = info->rx_ctrl->rssi;
    msg.data[data_len] = '\0';
    xQueueSendFromISR(s_rx_queue, &msg, NULL);
}

static void espnow_tx_cb(const uint8_t *mac, esp_now_send_status_t status)
{
    s_api->log(TAG, status == ESP_NOW_SEND_SUCCESS ? "TX OK" : "TX FAIL");
}

static void add_peer(const uint8_t *mac)
{
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.encrypt = false;  // Enable and set LMK for AES-128 in production
    if (esp_now_is_peer_exist(mac))
        esp_now_del_peer(mac);
    esp_now_add_peer(&peer);
}

static void send_command(const char *cmd)
{
    if (!s_peer_set) {
        s_api->display_print(TAG, "No peer set. Enter peer MAC first.");
        return;
    }
    esp_err_t err = esp_now_send(s_peer_mac, (const uint8_t *)cmd, strlen(cmd) + 1);
    char log[64];
    snprintf(log, sizeof(log), "TX -> %s: %s",
             err == ESP_OK ? "queued" : "error", cmd);
    s_api->display_print(TAG, log);
}

static void espnow_task(void *arg)
{
    s_rx_queue = xQueueCreate(8, sizeof(rx_msg_t));

    // WiFi must be initialized for ESP-NOW
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    esp_now_init();
    esp_now_register_send_cb(espnow_tx_cb);
    esp_now_register_recv_cb(espnow_rx_cb);

    s_api->display_print(TAG,
        "ESP-NOW C2 ready.\n"
        "Set peer MAC then type commands.\n"
        "No AP association needed.");

    // Demo: broadcast ping to FF:FF:FF:FF:FF:FF
    uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    esp_now_peer_info_t bc_peer = {};
    memcpy(bc_peer.peer_addr, broadcast, 6);
    bc_peer.channel = 0;
    bc_peer.encrypt = false;
    esp_now_add_peer(&bc_peer);

    // Send beacon
    const char *beacon = "ctOS-C2-BEACON";
    esp_now_send(broadcast, (const uint8_t *)beacon, strlen(beacon) + 1);
    s_api->log(TAG, "Beacon sent on broadcast");

    rx_msg_t rx;
    char log_buf[128];
    while (true) {
        if (xQueueReceive(s_rx_queue, &rx, pdMS_TO_TICKS(1000)) == pdTRUE) {
            snprintf(log_buf, sizeof(log_buf),
                "RX [%02X:%02X:%02X:%02X:%02X:%02X] RSSI=%d: %s",
                rx.src_mac[0], rx.src_mac[1], rx.src_mac[2],
                rx.src_mac[3], rx.src_mac[4], rx.src_mac[5],
                rx.rssi, (char *)rx.data);
            s_api->display_print(TAG, log_buf);
            s_api->log(TAG, log_buf);
        }
    }
}

extern "C" esp_err_t module_main(const ctos_api_t *api)
{
    s_api = api;
    api->log(TAG, "ESP-NOW C2 starting");
    xTaskCreate(espnow_task, TAG, 8192, nullptr, 5, nullptr);
    return ESP_OK;
}
