#include "file_browser.h"
#include "esp_log.h"
#include "M5Unified.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "cardputer_keyboard.h"

static const char *TAG = "file_browser";

#define MAX_ENTRIES 64
#define LINE_HEIGHT 12

typedef struct {
    char     name[64];
    bool     is_dir;
    uint32_t size;
} entry_t;

static entry_t s_entries[MAX_ENTRIES];
static int     s_entry_count = 0;
static int     s_cursor      = 0;
static int     s_scroll      = 0;
static bool    s_load_error  = false;

static void load_dir(const char *path)
{
    s_entry_count = 0;
    s_cursor      = 0;
    s_scroll      = 0;
    s_load_error  = false;

    DIR *d = opendir(path);
    if (!d) {
        s_load_error = true;
        ESP_LOGE(TAG, "Cannot open dir: %s", path);
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) && s_entry_count < MAX_ENTRIES) {
        if (strcmp(ent->d_name, ".") == 0) continue;

        strlcpy(s_entries[s_entry_count].name, ent->d_name,
                sizeof(s_entries[0].name));
        s_entries[s_entry_count].is_dir = (ent->d_type == DT_DIR);
        s_entries[s_entry_count].size   = 0;

        if (!s_entries[s_entry_count].is_dir) {
            char full[384];
            snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);
            struct stat st;
            if (stat(full, &st) == 0)
                s_entries[s_entry_count].size = (uint32_t)st.st_size;
        }
        s_entry_count++;
    }
    closedir(d);
    ESP_LOGI(TAG, "Loaded %d entries from %s", s_entry_count, path);
}

static void render(const char *path)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.printf("Files: %s\n", path);

    if (s_load_error) {
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.print("  Cannot open path.\n");
        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.print("  SD card inserted?\n");
    } else if (s_entry_count == 0) {
        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.print("  (empty)\n");
    } else {
        int visible = (M5.Display.height() - 24) / LINE_HEIGHT;
        for (int i = 0; i < visible && (s_scroll + i) < s_entry_count; i++) {
            int idx = s_scroll + i;
            bool selected = (idx == s_cursor);

            M5.Display.setTextColor(selected ? TFT_BLACK : TFT_WHITE,
                                    selected ? TFT_WHITE : TFT_BLACK);
            M5.Display.setCursor(0, 14 + i * LINE_HEIGHT);

            if (s_entries[idx].is_dir) {
                M5.Display.printf("[%s/]", s_entries[idx].name);
            } else {
                M5.Display.printf("%s  %lu B",
                    s_entries[idx].name, s_entries[idx].size);
            }
        }
    }

    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(0, M5.Display.height() - 10);
    M5.Display.print("[,/.] nav [Ent] open [Del] del [` ] back");
}

/* ── Root-location picker ────────────────────────────────────────────────── */
/* Shows a simple 2-option menu: SD card / Internal flash.
 * Returns the chosen path, or NULL if the user pressed back. */
static const char *pick_location(void)
{
    static const char *const LOCS[]   = { "/sdcard", "/spiffs" };
    static const char *const LABELS[] = { "SD Card  (/sdcard)",
                                          "Internal (/spiffs)" };
    int sel = 0;

    // Wait for the triggering key to be released before showing menu
    do { vTaskDelay(pdMS_TO_TICKS(50)); CardputerKb.update(); }
    while (CardputerKb.isPressed());

    while (true) {
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
        M5.Display.setCursor(0, 0);
        M5.Display.print("Browse files\n");

        for (int i = 0; i < 2; i++) {
            bool hi = (i == sel);
            M5.Display.setTextColor(hi ? TFT_BLACK : TFT_WHITE,
                                    hi ? TFT_WHITE : TFT_BLACK);
            M5.Display.printf("  %s\n", LABELS[i]);
        }

        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.setCursor(0, M5.Display.height() - 10);
        M5.Display.print("[,/.] nav  [Ent] open  [` ] back");

        vTaskDelay(pdMS_TO_TICKS(50));
        CardputerKb.update();

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) continue;

        auto kb  = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];

        if (key == ',' || key == ';') sel = 0;
        else if (key == '.' || key == '/') sel = 1;
        else if (key == '\n' || key == '\r') return LOCS[sel];
        else if (key == '`') return NULL;

        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

/* Suppress GCC's conservative format-truncation false-positives */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"

void ui_file_browser_show(const char *root_path)
{
    const char *start = root_path ? root_path : pick_location();
    if (!start) return;

    char path_stack[8][512];
    int  depth = 0;
    strlcpy(path_stack[0], start, sizeof(path_stack[0]));

    load_dir(path_stack[depth]);

    // Wait for triggering key release before entering nav loop
    do { vTaskDelay(pdMS_TO_TICKS(50)); CardputerKb.update(); }
    while (CardputerKb.isPressed());

    while (true) {
        M5.update();
        CardputerKb.update();
        render(path_stack[depth]);

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        auto kb = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];
        int visible = (M5.Display.height() - 24) / LINE_HEIGHT;

        if (key == ',' || key == ';') {
            if (s_cursor > 0) {
                s_cursor--;
                if (s_cursor < s_scroll) s_scroll = s_cursor;
            }
        } else if (key == '.' || key == '/') {
            if (s_cursor < s_entry_count - 1) {
                s_cursor++;
                if (s_cursor >= s_scroll + visible)
                    s_scroll = s_cursor - visible + 1;
            }
        } else if (key == '\n' || key == '\r') {
            if (s_cursor < s_entry_count && s_entries[s_cursor].is_dir
                && depth < 7) {
                depth++;
                char tmp[512];
                snprintf(tmp, sizeof(tmp), "%s/%s",
                         path_stack[depth - 1],
                         s_entries[s_cursor].name);
                strlcpy(path_stack[depth], tmp, sizeof(path_stack[depth]));
                load_dir(path_stack[depth]);
            }
        } else if (key == 127) {
            if (s_cursor < s_entry_count && !s_entries[s_cursor].is_dir) {
                char full[512];
                snprintf(full, sizeof(full), "%s/%s",
                         path_stack[depth], s_entries[s_cursor].name);
                remove(full);
                load_dir(path_stack[depth]);
            }
        } else if (key == '`') {
            if (depth > 0) {
                depth--;
                load_dir(path_stack[depth]);
            } else {
                return;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

#pragma GCC diagnostic pop
