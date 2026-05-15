/*
 * ctOS ESP-NOW Implant Firmware
 *
 * Minimal firmware for a planted ESP32 that receives ESP-NOW commands
 * from the ctOS Cardputer C2 controller (espnow_c2 module).
 *
 * Build separately with idf.py; target is any ESP32 variant.
 *
 * Commands received as null-terminated strings:
 *   "gpio:<pin>:<0|1>"   — set GPIO pin high or low
 *   "ping"               — respond with "pong"
 *   "info"               — respond with chip info
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "implant";

// MAC address of the ctOS Cardputer controller — set before flashing
static uint8_t CONTROLLER_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static void send_response(const char *msg)
{
    esp_now_send(CONTROLLER_MAC, (uint8_t *)msg, strlen(msg) + 1);
}

static void handle_command(const char *cmd)
{
    ESP_LOGI(TAG, "CMD: %s", cmd);

    if (strcmp(cmd, "ping") == 0) {
        send_response("pong");

    } else if (strcmp(cmd, "info") == 0) {
        char buf[64];
        esp_chip_info_t chip;
        esp_chip_info(&chip);
        snprintf(buf, sizeof(buf), "cores=%d rev=%d", chip.cores, chip.revision);
        send_response(buf);

    } else if (strncmp(cmd, "gpio:", 5) == 0) {
        int pin, val;
        if (sscanf(cmd + 5, "%d:%d", &pin, &val) == 2) {
            gpio_set_direction(pin, GPIO_MODE_OUTPUT);
            gpio_set_level(pin, val);
            send_response("ok");
        } else {
            send_response("err:bad_gpio_arg");
        }
    } else {
        send_response("err:unknown_cmd");
    }
}

static void espnow_rx_cb(const esp_now_recv_info_t *info,
                          const uint8_t *data, int data_len)
{
    if (data_len < 1 || data_len > 250) return;
    char cmd[256];
    memcpy(cmd, data, data_len);
    cmd[data_len] = '\0';
    handle_command(cmd);
}

void app_main(void)
{
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    esp_now_init();
    esp_now_register_recv_cb(espnow_rx_cb);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, CONTROLLER_MAC, 6);
    peer.channel = 0;
    peer.encrypt = false; // Enable and set PMK/LMK for production use
    esp_now_add_peer(&peer);

    ESP_LOGI(TAG, "ctOS implant ready. Listening for ESP-NOW commands.");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
