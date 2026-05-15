#include "module_api.h"
#include "esp_log.h"
#include "esp_hid_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>

static const char *TAG = "badusb";
static const ctos_api_t *s_api = nullptr;

// HID key codes (USB HID Usage Tables — Keyboard/Keypad Page 0x07)
#define HID_ENTER  0x28
#define HID_TAB    0x2B
#define HID_SPACE  0x2C
#define HID_DEL    0x4C
#define HID_GUI    0xE3  // Modifier
#define HID_CTRL   0xE0
#define HID_ALT    0xE2
#define HID_SHIFT  0xE1

// Stub USB HID send — replace with esp_hid_device_send() once USB HID task is up
static void hid_send_key(uint8_t modifier, uint8_t keycode)
{
    uint8_t report[8] = { modifier, 0, keycode, 0, 0, 0, 0, 0 };
    // esp_hid_device_send(HID_REPORT_TYPE_INPUT, 0x01, report, sizeof(report));
    (void)report;
    vTaskDelay(pdMS_TO_TICKS(5));

    // Key release
    uint8_t release[8] = {};
    // esp_hid_device_send(HID_REPORT_TYPE_INPUT, 0x01, release, sizeof(release));
    (void)release;
}

static void hid_send_char(char c)
{
    // Basic ASCII → HID keycode (a-z, 0-9, common punctuation)
    uint8_t modifier = 0;
    uint8_t keycode  = 0;

    if (c >= 'a' && c <= 'z') {
        keycode = 0x04 + (c - 'a');
    } else if (c >= 'A' && c <= 'Z') {
        keycode  = 0x04 + (c - 'A');
        modifier = HID_SHIFT;
    } else if (c >= '1' && c <= '9') {
        keycode = 0x1E + (c - '1');
    } else if (c == '0') {
        keycode = 0x27;
    } else if (c == ' ') {
        keycode = HID_SPACE;
    } else if (c == '\n') {
        keycode = HID_ENTER;
    } else if (c == '\t') {
        keycode = HID_TAB;
    }
    // (extend as needed for punctuation)

    if (keycode) hid_send_key(modifier, keycode);
}

static void hid_type_string(const char *str, uint32_t char_delay_ms)
{
    while (*str) {
        hid_send_char(*str++);
        vTaskDelay(pdMS_TO_TICKS(char_delay_ms));
    }
}

// Minimal DuckyScript parser
static void run_ducky_script(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        s_api->log(TAG, "Script not found");
        return;
    }

    char line[256];
    uint32_t default_delay = 50;

    while (fgets(line, sizeof(line), f)) {
        // Strip trailing newline
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;

        if (strncmp(line, "STRING ", 7) == 0) {
            hid_type_string(line + 7, default_delay);

        } else if (strncmp(line, "DELAY ", 6) == 0) {
            vTaskDelay(pdMS_TO_TICKS(atoi(line + 6)));

        } else if (strcmp(line, "ENTER") == 0) {
            hid_send_key(0, HID_ENTER);

        } else if (strcmp(line, "TAB") == 0) {
            hid_send_key(0, HID_TAB);

        } else if (strncmp(line, "CTRL ", 5) == 0) {
            // CTRL <KEY>
            char k = line[5];
            uint8_t kc = (k >= 'a' && k <= 'z') ? 0x04 + (k - 'a') :
                         (k >= 'A' && k <= 'Z') ? 0x04 + (k - 'A') : 0;
            if (kc) hid_send_key(HID_CTRL, kc);

        } else if (strncmp(line, "GUI ", 4) == 0) {
            char k = line[4];
            uint8_t kc = (k >= 'a' && k <= 'z') ? 0x04 + (k - 'a') : 0;
            if (kc) hid_send_key(HID_GUI, kc);

        } else if (strncmp(line, "ALT ", 4) == 0) {
            char k = line[4];
            uint8_t kc = (k >= 'a' && k <= 'z') ? 0x04 + (k - 'a') : 0;
            if (kc) hid_send_key(HID_ALT, kc);

        } else if (strncmp(line, "DEFAULT_DELAY ", 14) == 0) {
            default_delay = (uint32_t)atoi(line + 14);

        } else if (strncmp(line, "REPEAT ", 7) == 0) {
            // Not fully implemented — would need last-command tracking
        }

        char log_line[64];
        snprintf(log_line, sizeof(log_line), "> %s", line);
        s_api->display_print(TAG, log_line);
    }
    fclose(f);
    s_api->display_print(TAG, "Script complete.");
}

static void list_payloads(char names[][64], int *count)
{
    *count = 0;
    DIR *d = opendir("/sdcard/payloads");
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) && *count < 16) {
        if (strstr(ent->d_name, ".ducky")) {
            strlcpy(names[*count], ent->d_name, 64);
            (*count)++;
        }
    }
    closedir(d);
}

static void badusb_task(void *arg)
{
    // USB HID init would go here:
    // esp_hid_device_config_t hid_cfg = { ... };
    // esp_hid_device_init(&hid_cfg);
    s_api->log(TAG, "USB HID ready (placeholder — wire esp_hid_device)");

    char names[16][64];
    int  count = 0;
    list_payloads(names, &count);

    if (count == 0) {
        s_api->display_print(TAG, "No .ducky payloads on SD card.\n/sdcard/payloads/");
    } else {
        char menu[512];
        int off = snprintf(menu, sizeof(menu), "Payloads:\n");
        for (int i = 0; i < count; i++)
            off += snprintf(menu + off, sizeof(menu) - off, "[%d] %s\n", i + 1, names[i]);
        s_api->display_print(TAG, menu);

        // Auto-run first payload for demo
        char path[128];
        snprintf(path, sizeof(path), "/sdcard/payloads/%s", names[0]);
        s_api->log(TAG, "Executing payload");
        vTaskDelay(pdMS_TO_TICKS(2000));
        run_ducky_script(path);
    }

    while (true) vTaskDelay(pdMS_TO_TICKS(5000));
}

extern "C" esp_err_t module_main(const ctos_api_t *api)
{
    s_api = api;
    api->log(TAG, "BadUSB module starting");
    xTaskCreate(badusb_task, TAG, 8192, nullptr, 5, nullptr);
    return ESP_OK;
}
