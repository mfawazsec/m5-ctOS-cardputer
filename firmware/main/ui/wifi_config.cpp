#include "wifi_config.h"
#include "settings/config.h"
#include "wifi/hotspot.h"
#include "M5Unified.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cardputer_keyboard.h"
#include <string.h>

static void render_wifi(int field, bool editing,
                        const char *ssid, const char *pass)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.print("WiFi Config\n");

    // Editable fields
    const char *labels[2] = { "SSID", "Pass" };
    const char *vals[2]   = { ssid,   pass   };
    for (int i = 0; i < 2; i++) {
        bool sel = (i == field);
        M5.Display.setTextColor(sel ? TFT_BLACK : TFT_WHITE,
                                sel ? TFT_WHITE : TFT_BLACK);
        if (sel && editing)
            M5.Display.printf("%s: %s_\n", labels[i], vals[i]);
        else
            M5.Display.printf("%s: %s\n",  labels[i], vals[i]);
    }

    // Connection info (read-only)
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.printf("AP:  %s\n", hotspot_is_running() ? "RUNNING" : "STOPPED");
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.print("IP:  192.168.4.1\n");
    M5.Display.printf("PIN: %s\n", config_get_pin_display());

    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(0, M5.Display.height() - 10);
    if (editing)
        M5.Display.print("[type] input  [Del] del char  [Ent] save");
    else
        M5.Display.print("[Tab]field [Ent]edit [W]AP [`]back");
}

void ui_wifi_config_show(void)
{
    char ssid[33];
    char pass[64];
    strlcpy(ssid, config_get_wifi_ssid(),     sizeof(ssid));
    strlcpy(pass, config_get_wifi_password(), sizeof(pass));

    int  field   = 0;
    bool editing = false;

    // Drain the key that opened this screen
    do { vTaskDelay(pdMS_TO_TICKS(50)); CardputerKb.update(); }
    while (CardputerKb.isPressed());

    while (true) {
        render_wifi(field, editing, ssid, pass);

        vTaskDelay(pdMS_TO_TICKS(50));
        CardputerKb.update();

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) continue;

        auto kb  = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];

        if (!editing) {
            if (key == '`') return;

            if (key == '\t') field = 1 - field;

            if (key == '\n' || key == '\r') {
                // Clear field and enter edit mode
                if (field == 0) ssid[0] = '\0';
                else             pass[0] = '\0';
                editing = true;
            }

            if (key == 'w' || key == 'W') {
                if (hotspot_is_running()) hotspot_stop();
                else                      hotspot_start();
            }

            vTaskDelay(pdMS_TO_TICKS(150));
        } else {
            char  *buf    = (field == 0) ? ssid   : pass;
            size_t buflen = (field == 0) ? sizeof(ssid) : sizeof(pass);
            size_t len    = strlen(buf);

            if (key == '\n' || key == '\r') {
                if (field == 0) config_set_wifi_ssid(ssid);
                else             config_set_wifi_password(pass);
                editing = false;
                vTaskDelay(pdMS_TO_TICKS(150));
            } else if (key == '`') {
                // Cancel: restore saved value
                strlcpy(ssid, config_get_wifi_ssid(),     sizeof(ssid));
                strlcpy(pass, config_get_wifi_password(), sizeof(pass));
                editing = false;
                vTaskDelay(pdMS_TO_TICKS(150));
            } else if (key == 127 && len > 0) {
                buf[len - 1] = '\0';
                vTaskDelay(pdMS_TO_TICKS(100)); // allow key-repeat feel
            } else if (key >= 32 && key < 127 && len < buflen - 1) {
                buf[len]     = key;
                buf[len + 1] = '\0';
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
    }
}
