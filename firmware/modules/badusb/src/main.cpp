#include "module_api.h"
#include "registry.h"
#include "loader.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "M5Unified.h"
#include "cardputer_keyboard.h"
#include "ui/mod_common.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "badusb";
static const char *ID  = "badusb";
static const ctos_api_t *s_api = nullptr;

// HID keycodes
#define HID_ENTER  0x28
#define HID_ESC    0x29
#define HID_BS     0x2A
#define HID_TAB    0x2B
#define HID_SPACE  0x2C
#define HID_DEL    0x4C
#define HID_UP     0x52
#define HID_DOWN   0x51
#define HID_LEFT   0x50
#define HID_RIGHT  0x4F
#define HID_F1     0x3A

// Modifier masks
#define MOD_CTRL   0x01
#define MOD_SHIFT  0x02
#define MOD_ALT    0x04
#define MOD_GUI    0x08

static volatile bool s_executing  = false;
static volatile int  s_exec_index = -1;
static char  s_names[16][64]  = {};
static int   s_payload_count  = 0;
static bool  s_hid_active     = false;  // true when USB HID is initialized

// ---------------------------------------------------------------------------
// Built-in payloads written to SD on first run
// ---------------------------------------------------------------------------
static const struct { const char *name; const char *body; } k_builtin_payloads[] = {
    {
        "win11_hello.ducky",
        "DELAY 1000\n"
        "GUI r\n"
        "DELAY 600\n"
        "STRING powershell -WindowStyle Hidden -c \""
            "Add-Type -AssemblyName System.Windows.Forms; "
            "[System.Windows.Forms.MessageBox]::Show("
            "'ctOS was here! Your defenses have been tested.','ctOS v2.0')\"\n"
        "ENTER\n"
    },
    {
        "win11_rickroll.ducky",
        "DELAY 500\n"
        "GUI r\n"
        "DELAY 500\n"
        "STRING msedge --new-window https://youtu.be/dQw4w9WgXcQ\n"
        "ENTER\n"
    },
    {
        "win11_wifi_dump.ducky",
        "DELAY 1000\n"
        "GUI r\n"
        "DELAY 500\n"
        "STRING cmd /c netsh wlan show profiles > %TEMP%\\wifi.txt && notepad %TEMP%\\wifi.txt\n"
        "ENTER\n"
    },
    {
        "macos_say.ducky",
        "DELAY 500\n"
        "GUI SPACE\n"
        "DELAY 700\n"
        "STRING terminal\n"
        "ENTER\n"
        "DELAY 1200\n"
        "STRING say -v Alex \"Greetings from ctOS. Your security has been evaluated.\"\n"
        "ENTER\n"
    },
    {
        "macos_wallpaper_nasa.ducky",
        "DELAY 500\n"
        "GUI SPACE\n"
        "DELAY 700\n"
        "STRING safari\n"
        "ENTER\n"
        "DELAY 1200\n"
        "STRING https://apod.nasa.gov\n"
        "ENTER\n"
    },
    {
        "android_browser.ducky",
        "DELAY 500\n"
        "STRING https://ctOS.dev\n"
        "ENTER\n"
    },
    {
        "ctOS_signature.ducky",
        "DELAY 500\n"
        "STRING ctOS v2.0 | m5-ctos-cardputer | github.com/slmshdy\n"
        "ENTER\n"
    },
};
static const int k_builtin_count = (int)(sizeof(k_builtin_payloads) / sizeof(k_builtin_payloads[0]));

static void write_builtin_payloads(void)
{
    for (int i = 0; i < k_builtin_count; i++) {
        char path[80];
        snprintf(path, sizeof(path), "/sdcard/payloads/%s", k_builtin_payloads[i].name);
        struct stat st;
        if (stat(path, &st) == 0) continue;  // already exists
        FILE *f = fopen(path, "w");
        if (!f) continue;
        fputs(k_builtin_payloads[i].body, f);
        fclose(f);
    }
}

// ---------------------------------------------------------------------------
// HID send (stub — logs; replace with TinyUSB HID device report when ready)
// ---------------------------------------------------------------------------
static void hid_send_key(uint8_t modifier, uint8_t keycode)
{
    if (!s_hid_active) {
        char msg[48];
        snprintf(msg, sizeof(msg), "[USB HID pending] mod=%02x kc=%02x", modifier, keycode);
        s_api->log(ID, msg);
        vTaskDelay(pdMS_TO_TICKS(5));
        return;
    }
    // TODO: send via TinyUSB HID report when implemented
    vTaskDelay(pdMS_TO_TICKS(5));
}

static void hid_send_char(char c)
{
    uint8_t mod = 0, kc = 0;
    if (c >= 'a' && c <= 'z')       { kc = 0x04 + (c - 'a'); }
    else if (c >= 'A' && c <= 'Z')  { kc = 0x04 + (c - 'A'); mod = MOD_SHIFT; }
    else if (c >= '1' && c <= '9')  { kc = 0x1E + (c - '1'); }
    else if (c == '0')               { kc = 0x27; }
    else if (c == ' ')               { kc = HID_SPACE; }
    else if (c == '\n')              { kc = HID_ENTER; }
    else if (c == '\t')              { kc = HID_TAB; }
    else if (c == '-')               { kc = 0x2D; }
    else if (c == '=')               { kc = 0x2E; }
    else if (c == '[')               { kc = 0x2F; }
    else if (c == ']')               { kc = 0x30; }
    else if (c == '\\')              { kc = 0x31; }
    else if (c == ';')               { kc = 0x33; }
    else if (c == '\'')              { kc = 0x34; }
    else if (c == '`')               { kc = 0x35; }
    else if (c == ',')               { kc = 0x36; }
    else if (c == '.')               { kc = 0x37; }
    else if (c == '/')               { kc = 0x38; }
    else if (c == '!')               { kc = 0x1E; mod = MOD_SHIFT; }
    else if (c == '@')               { kc = 0x1F; mod = MOD_SHIFT; }
    else if (c == '#')               { kc = 0x20; mod = MOD_SHIFT; }
    else if (c == '$')               { kc = 0x21; mod = MOD_SHIFT; }
    else if (c == '%')               { kc = 0x22; mod = MOD_SHIFT; }
    else if (c == '^')               { kc = 0x23; mod = MOD_SHIFT; }
    else if (c == '&')               { kc = 0x24; mod = MOD_SHIFT; }
    else if (c == '*')               { kc = 0x25; mod = MOD_SHIFT; }
    else if (c == '(')               { kc = 0x26; mod = MOD_SHIFT; }
    else if (c == ')')               { kc = 0x27; mod = MOD_SHIFT; }
    else if (c == '_')               { kc = 0x2D; mod = MOD_SHIFT; }
    else if (c == '+')               { kc = 0x2E; mod = MOD_SHIFT; }
    else if (c == '{')               { kc = 0x2F; mod = MOD_SHIFT; }
    else if (c == '}')               { kc = 0x30; mod = MOD_SHIFT; }
    else if (c == '|')               { kc = 0x31; mod = MOD_SHIFT; }
    else if (c == ':')               { kc = 0x33; mod = MOD_SHIFT; }
    else if (c == '"')               { kc = 0x34; mod = MOD_SHIFT; }
    else if (c == '~')               { kc = 0x35; mod = MOD_SHIFT; }
    else if (c == '<')               { kc = 0x36; mod = MOD_SHIFT; }
    else if (c == '>')               { kc = 0x37; mod = MOD_SHIFT; }
    else if (c == '?')               { kc = 0x38; mod = MOD_SHIFT; }
    if (kc) hid_send_key(mod, kc);
}

static void hid_type_string(const char *str, uint32_t delay_ms)
{
    while (*str) { hid_send_char(*str++); vTaskDelay(pdMS_TO_TICKS(delay_ms)); }
}

static uint8_t resolve_named_key(const char *name)
{
    if (!strcmp(name, "ENTER"))    return HID_ENTER;
    if (!strcmp(name, "SPACE"))    return HID_SPACE;
    if (!strcmp(name, "TAB"))      return HID_TAB;
    if (!strcmp(name, "ESC"))      return HID_ESC;
    if (!strcmp(name, "ESCAPE"))   return HID_ESC;
    if (!strcmp(name, "DELETE"))   return HID_DEL;
    if (!strcmp(name, "UP"))       return HID_UP;
    if (!strcmp(name, "DOWN"))     return HID_DOWN;
    if (!strcmp(name, "LEFT"))     return HID_LEFT;
    if (!strcmp(name, "RIGHT"))    return HID_RIGHT;
    for (int i = 0; i < 12; i++) {
        char fn[4]; snprintf(fn, sizeof(fn), "F%d", i + 1);
        if (!strcmp(name, fn)) return (uint8_t)(HID_F1 + i);
    }
    if (strlen(name) == 1) {
        char c = name[0];
        if (c >= 'a' && c <= 'z') return (uint8_t)(0x04 + (c - 'a'));
        if (c >= 'A' && c <= 'Z') return (uint8_t)(0x04 + (c - 'A'));
    }
    return 0;
}

static void run_ducky_script(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { s_api->log(ID, "Script not found"); return; }

    char line[256];
    uint32_t default_delay = 50;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || line[0] == '#' || line[0] == '/') continue;

        char log_line[64];
        snprintf(log_line, sizeof(log_line), "> %.58s", line);
        s_api->display_print(ID, log_line);

        if (!strncmp(line, "STRING ", 7)) {
            hid_type_string(line + 7, default_delay);
        } else if (!strncmp(line, "DELAY ", 6)) {
            vTaskDelay(pdMS_TO_TICKS(atoi(line + 6)));
        } else if (!strncmp(line, "DEFAULT_DELAY ", 14)) {
            default_delay = (uint32_t)atoi(line + 14);
        } else if (!strcmp(line, "ENTER")) {
            hid_send_key(0, HID_ENTER);
        } else if (!strcmp(line, "TAB")) {
            hid_send_key(0, HID_TAB);
        } else if (!strcmp(line, "SPACE")) {
            hid_send_key(0, HID_SPACE);
        } else if (!strcmp(line, "ESCAPE") || !strcmp(line, "ESC")) {
            hid_send_key(0, HID_ESC);
        } else if (!strcmp(line, "DELETE")) {
            hid_send_key(0, HID_DEL);
        } else if (!strcmp(line, "UP"))    { hid_send_key(0, HID_UP); }
        else if (!strcmp(line, "DOWN"))    { hid_send_key(0, HID_DOWN); }
        else if (!strcmp(line, "LEFT"))    { hid_send_key(0, HID_LEFT); }
        else if (!strcmp(line, "RIGHT"))   { hid_send_key(0, HID_RIGHT); }
        else if (!strncmp(line, "CTRL-", 5) || !strncmp(line, "CTRL ", 5)) {
            uint8_t kc = resolve_named_key(line + 5);
            if (kc) hid_send_key(MOD_CTRL, kc);
        } else if (!strncmp(line, "GUI SPACE", 9)) {
            hid_send_key(MOD_GUI, HID_SPACE);
        } else if (!strncmp(line, "GUI-SPACE", 9)) {
            hid_send_key(MOD_GUI, HID_SPACE);
        } else if (!strncmp(line, "GUI ", 4)) {
            uint8_t kc = resolve_named_key(line + 4);
            if (kc) hid_send_key(MOD_GUI, kc);
        } else if (!strncmp(line, "ALT-", 4) || !strncmp(line, "ALT ", 4)) {
            uint8_t kc = resolve_named_key(line + 4);
            if (kc) hid_send_key(MOD_ALT, kc);
        } else if (!strncmp(line, "SHIFT-", 6) || !strncmp(line, "SHIFT ", 6)) {
            uint8_t kc = resolve_named_key(line + 6);
            if (kc) hid_send_key(MOD_SHIFT, kc);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
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
    while ((ent = readdir(d)) && s_payload_count < 16) {
        if (strstr(ent->d_name, ".ducky"))
            strlcpy(s_names[s_payload_count++], ent->d_name, 64);
    }
    closedir(d);
}

static void badusb_task(void *arg)
{
    s_api->log(ID, "BadUSB: writing built-in payloads to SD...");
    write_builtin_payloads();
    list_payloads();

    if (!s_hid_active)
        s_api->log(ID, "USB HID stub active (TinyUSB TODO)");

    char msg[64];
    snprintf(msg, sizeof(msg), "%d payloads on /sdcard/payloads/", s_payload_count);
    s_api->display_print(ID, msg);

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
    s_hid_active = false;
    module_registry_set_running(ID, true);
    if (xTaskCreate(badusb_task, TAG, 8192, nullptr, 5, nullptr) != pdPASS) {
        module_registry_set_running(ID, false);
        ESP_LOGE(TAG, "xTaskCreate failed — free heap: %u B", (unsigned)esp_get_free_heap_size());
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

    TickType_t last_draw = 0;

    while (true) {
        M5.update();
        CardputerKb.update();

        if (log_view) { mod_show_log_view(ID, "BADUSB"); log_view = false; mod_drain_keys(); continue; }

        TickType_t now = xTaskGetTickCount();
        if ((now - last_draw) >= pdMS_TO_TICKS(250)) {
            if (s_payload_count == 0) list_payloads();

            M5.Display.fillScreen(TFT_BLACK);
            M5.Display.setTextColor(TFT_RED, TFT_BLACK);
            M5.Display.setCursor(0, 0);
            if (s_executing)
                M5.Display.print("BADUSB  <EXECUTING>");
            else if (!s_hid_active)
                M5.Display.print("BADUSB  [HID:stub]");
            else
                M5.Display.print("BADUSB  READY");

            M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

            if (s_payload_count == 0) {
                M5.Display.setCursor(0, 14);
                M5.Display.print("No payloads found.");
                M5.Display.setCursor(0, 26);
                M5.Display.print("Put .ducky on:");
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
            M5.Display.print("[,.][Ent]run [R]rld [L][`]");

            last_draw = now;
        }

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
        last_draw = 0;
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
