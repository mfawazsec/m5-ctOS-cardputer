#include "module_api.h"
#include "registry.h"
#include "loader.h"
#include "esp_log.h"
#include "esp_hid_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "M5Unified.h"
#include "cardputer_keyboard.h"
#include "ui/mod_common.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>

static const char *TAG = "badusb";
static const char *ID  = "badusb";
static const ctos_api_t *s_api = nullptr;

#define HID_ENTER  0x28
#define HID_TAB    0x2B
#define HID_SPACE  0x2C
#define HID_GUI    0xE3
#define HID_CTRL   0xE0
#define HID_ALT    0xE2
#define HID_SHIFT  0xE1

static volatile bool s_executing  = false;
static volatile int  s_exec_index = -1;
static char  s_names[16][64]  = {};
static int   s_payload_count  = 0;

static void hid_send_key(uint8_t modifier, uint8_t keycode)
{
    // USB HID device send — requires tinyusb HID device task
    // Stub: logs the key that would be sent
    char msg[32];
    snprintf(msg, sizeof(msg), "HID mod=%02x kc=%02x", modifier, keycode);
    s_api->log(ID, msg);
    vTaskDelay(pdMS_TO_TICKS(5));
}

static void hid_send_char(char c)
{
    uint8_t mod = 0, kc = 0;
    if (c >= 'a' && c <= 'z')       { kc = 0x04 + (c - 'a'); }
    else if (c >= 'A' && c <= 'Z')  { kc = 0x04 + (c - 'A'); mod = HID_SHIFT; }
    else if (c >= '1' && c <= '9')  { kc = 0x1E + (c - '1'); }
    else if (c == '0')               { kc = 0x27; }
    else if (c == ' ')               { kc = HID_SPACE; }
    else if (c == '\n')              { kc = HID_ENTER; }
    else if (c == '\t')              { kc = HID_TAB; }
    if (kc) hid_send_key(mod, kc);
}

static void hid_type_string(const char *str, uint32_t delay_ms)
{
    while (*str) { hid_send_char(*str++); vTaskDelay(pdMS_TO_TICKS(delay_ms)); }
}

static void run_ducky_script(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { s_api->log(ID, "Script not found"); return; }

    char line[256];
    uint32_t default_delay = 50;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || line[0] == '#') continue;

        if      (!strncmp(line, "STRING ", 7))        hid_type_string(line+7, default_delay);
        else if (!strncmp(line, "DELAY ", 6))         vTaskDelay(pdMS_TO_TICKS(atoi(line+6)));
        else if (!strcmp(line, "ENTER"))               hid_send_key(0, HID_ENTER);
        else if (!strcmp(line, "TAB"))                 hid_send_key(0, HID_TAB);
        else if (!strncmp(line, "CTRL ", 5)) {
            char k = line[5];
            uint8_t kc = (k >= 'a' && k <= 'z') ? 0x04+(k-'a') :
                         (k >= 'A' && k <= 'Z') ? 0x04+(k-'A') : 0;
            if (kc) hid_send_key(HID_CTRL, kc);
        } else if (!strncmp(line, "GUI ", 4)) {
            char k = line[4];
            uint8_t kc = (k >= 'a' && k <= 'z') ? 0x04+(k-'a') : 0;
            if (kc) hid_send_key(HID_GUI, kc);
        } else if (!strncmp(line, "ALT ", 4)) {
            char k = line[4];
            uint8_t kc = (k >= 'a' && k <= 'z') ? 0x04+(k-'a') : 0;
            if (kc) hid_send_key(HID_ALT, kc);
        } else if (!strncmp(line, "DEFAULT_DELAY ", 14)) {
            default_delay = (uint32_t)atoi(line+14);
        }

        char log_line[64];
        snprintf(log_line, sizeof(log_line), "> %.60s", line);
        s_api->display_print(ID, log_line);
    }
    fclose(f);
    s_api->display_print(ID, "Script complete.");
}

static void list_payloads(void)
{
    s_payload_count = 0;
    DIR *d = opendir("/sdcard/payloads");
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) && s_payload_count < 16)
        if (strstr(ent->d_name, ".ducky"))
            strlcpy(s_names[s_payload_count++], ent->d_name, 64);
    closedir(d);
}

static void badusb_task(void *arg)
{
    s_api->log(ID, "BadUSB: USB HID device mode (tinyusb)");
    list_payloads();

    if (s_payload_count == 0)
        s_api->display_print(ID, "No .ducky files on /sdcard/payloads/");
    else {
        char msg[64];
        snprintf(msg, sizeof(msg), "%d payloads found", s_payload_count);
        s_api->display_print(ID, msg);
    }

    while (module_registry_is_running(ID)) {
        int idx = s_exec_index;
        if (idx >= 0 && idx < s_payload_count) {
            s_exec_index = -1;
            s_executing  = true;
            char path[128];
            snprintf(path, sizeof(path), "/sdcard/payloads/%s", s_names[idx]);
            run_ducky_script(path);
            s_executing = false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    s_api->log(ID, "BadUSB stopped");
    module_registry_set_running(ID, false);
    vTaskDelete(NULL);
}

extern "C" esp_err_t badusb_main(const ctos_api_t *api)
{
    if (module_registry_is_running(ID)) return ESP_OK;
    s_api        = api;
    s_exec_index = -1;
    s_executing  = false;
    module_registry_set_running(ID, true);
    if (xTaskCreate(badusb_task, TAG, 8192, nullptr, 5, nullptr) != pdPASS) {
        module_registry_set_running(ID, false);
        return ESP_FAIL;
    }
    return ESP_OK;
}

extern "C" void badusb_ui_show(void)
{
    module_loader_start(ID);
    mod_drain_keys();
    bool log_view = false;
    int  cursor   = 0;

    while (true) {
        M5.update();
        CardputerKb.update();

        if (log_view) { mod_show_log_view(ID, "BADUSB"); log_view = false; mod_drain_keys(); continue; }

        // Refresh payload list if empty
        if (s_payload_count == 0) list_payloads();

        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.setCursor(0, 0);
        M5.Display.printf("BADUSB  %s", s_executing ? "<EXECUTING>" : "READY");

        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

        if (s_payload_count == 0) {
            M5.Display.setCursor(0, 14);
            M5.Display.print("No payloads found.");
            M5.Display.setCursor(0, 26);
            M5.Display.print("Put .ducky files in:");
            M5.Display.setCursor(0, 38);
            M5.Display.print("/sdcard/payloads/");
        } else {
            int visible = (M5.Display.height() - 26) / MOD_LH;
            for (int i = 0; i < visible && i < s_payload_count; i++) {
                bool sel = (i == cursor);
                M5.Display.setTextColor(sel ? TFT_BLACK : TFT_WHITE,
                                        sel ? TFT_RED   : TFT_BLACK);
                M5.Display.setCursor(0, 14 + i * MOD_LH);
                M5.Display.printf("%.26s", s_names[i]);
            }
        }

        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.setCursor(0, M5.Display.height() - 10);
        M5.Display.print("[,/.] [Ent]exec [R]reload [L]log");

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        auto kb = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];

        if (key == '`' || key == 27) return;
        else if (key == 'l' || key == 'L') log_view = true;
        else if (key == 'r' || key == 'R') { list_payloads(); cursor = 0; }
        else if ((key == ',' || key == ';') && cursor > 0) cursor--;
        else if ((key == '.' || key == '/') && cursor < s_payload_count - 1) cursor++;
        else if ((key == '\n' || key == '\r') && !s_executing && s_payload_count > 0)
            s_exec_index = cursor;
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
