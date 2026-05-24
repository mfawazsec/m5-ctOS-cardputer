#include "module_api.h"
#include "registry.h"
#include "loader.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "M5Unified.h"
#include "cardputer_keyboard.h"
#include "ui/mod_common.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "espnow_c2";
static const char *ID  = "espnow_c2";
static const ctos_api_t *s_api = nullptr;

static QueueHandle_t s_rx_queue   = nullptr;
static volatile uint32_t s_rx_count = 0;
static volatile uint32_t s_tx_count = 0;
static char s_last_rx[64] = {};

typedef struct {
    uint8_t src_mac[6];
    uint8_t data[250];
    int     data_len;
    int     rssi;
} rx_msg_t;

static void espnow_rx_cb(const esp_now_recv_info_t *info,
                          const uint8_t *data, int data_len)
{
    if (data_len <= 0 || data_len > 250 || !s_rx_queue) return;
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
    if (status == ESP_NOW_SEND_SUCCESS) s_tx_count++;
    s_api->log(ID, status == ESP_NOW_SEND_SUCCESS ? "TX OK" : "TX FAIL");
}

static void espnow_task(void *arg)
{
    // WiFi init — skip if already initialized by hotspot
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_ERR_WIFI_NOT_INIT) {
        esp_netif_init();
        esp_event_loop_create_default();
        esp_netif_create_default_wifi_sta();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
    }

    esp_now_init();
    esp_now_register_send_cb(espnow_tx_cb);
    esp_now_register_recv_cb(espnow_rx_cb);

    // Broadcast peer
    uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    esp_now_peer_info_t bc = {};
    memcpy(bc.peer_addr, broadcast, 6);
    bc.encrypt = false;
    esp_now_add_peer(&bc);

    const char *beacon = "ctOS-C2-BEACON";
    esp_now_send(broadcast, (const uint8_t *)beacon, strlen(beacon) + 1);
    s_api->log(ID, "Beacon sent");

    rx_msg_t rx;
    while (module_registry_is_running(ID)) {
        if (xQueueReceive(s_rx_queue, &rx, pdMS_TO_TICKS(1000)) == pdTRUE) {
            s_rx_count++;
            char buf[128];
            snprintf(buf, sizeof(buf), "RX [%02X:%02X:%02X:%02X:%02X:%02X] RSSI=%d: %.50s",
                     rx.src_mac[0],rx.src_mac[1],rx.src_mac[2],
                     rx.src_mac[3],rx.src_mac[4],rx.src_mac[5],
                     rx.rssi, (char *)rx.data);
            s_api->display_print(ID, buf);
            snprintf(s_last_rx, sizeof(s_last_rx), "%.60s", (char *)rx.data);
        }
    }

    esp_now_deinit();
    s_api->log(ID, "ESP-NOW C2 stopped");
    module_registry_set_running(ID, false);
    vTaskDelete(NULL);
}

extern "C" esp_err_t espnow_c2_main(const ctos_api_t *api)
{
    if (module_registry_is_running(ID)) return ESP_OK;
    s_api      = api;
    s_rx_count = 0;
    s_tx_count = 0;
    memset(s_last_rx, 0, sizeof(s_last_rx));

    if (!s_rx_queue) s_rx_queue = xQueueCreate(8, sizeof(rx_msg_t));
    if (!s_rx_queue) return ESP_ERR_NO_MEM;

    module_registry_set_running(ID, true);
    if (xTaskCreate(espnow_task, TAG, 8192, nullptr, 5, nullptr) != pdPASS) {
        module_registry_set_running(ID, false);
        return ESP_FAIL;
    }
    return ESP_OK;
}

extern "C" void espnow_c2_ui_show(void)
{
    module_loader_start(ID);
    mod_drain_keys();
    bool log_view = false;

    while (true) {
        M5.update();
        CardputerKb.update();

        if (log_view) { mod_show_log_view(ID, "ESP-NOW C2"); log_view = false; mod_drain_keys(); continue; }

        wifi_second_chan_t sc;
        uint8_t chan = 1;
        esp_wifi_get_channel(&chan, &sc);

        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5.Display.setCursor(0, 0); M5.Display.print("ESP-NOW C2 CHANNEL");

        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.setCursor(0, 14);
        M5.Display.printf("Chan: %d  [C]change", chan);
        M5.Display.setCursor(0, 26);
        M5.Display.printf("RX: %lu  TX: %lu", (unsigned long)s_rx_count, (unsigned long)s_tx_count);
        M5.Display.setCursor(0, 38);
        M5.Display.print("Peers: broadcast+added");
        M5.Display.setCursor(0, 50);
        if (s_last_rx[0])
            M5.Display.printf("Last: %.26s", s_last_rx);
        else
            M5.Display.print("Last: (waiting...)");
        M5.Display.setCursor(0, 62);
        M5.Display.print("No AP assoc. needed");

        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.setCursor(0, M5.Display.height() - 10);
        M5.Display.print("[B]beacon [C]chan [L]log [`]bk");

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        auto kb = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];

        if (key == '`' || key == 27) return;
        else if (key == 'l' || key == 'L') log_view = true;
        else if (key == 'b' || key == 'B') {
            uint8_t bc[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
            const char *msg = "ctOS-C2-BEACON";
            esp_now_send(bc, (const uint8_t *)msg, strlen(msg)+1);
        } else if (key == 'c' || key == 'C') {
            uint8_t next_ch = (chan % 13) + 1;
            esp_wifi_set_channel(next_ch, WIFI_SECOND_CHAN_NONE);
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
