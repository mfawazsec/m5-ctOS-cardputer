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

static void load_dir(const char *path)
{
    s_entry_count = 0;
    s_cursor      = 0;
    s_scroll      = 0;

    DIR *d = opendir(path);
    if (!d) {
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

    int visible = (M5.Display.height() - 14) / LINE_HEIGHT;
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

    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(0, M5.Display.height() - 10);
    M5.Display.print("[UP/DN] nav  [OK] open  [DEL] delete  [ESC] back");
}

/* Suppress GCC's conservative format-truncation false-positives.
 * path_stack entries are bounded by actual SD-card path lengths (<128 chars),
 * so path+"/"+name (max ~192 chars) fits comfortably in the 512-byte buffers.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"

void ui_file_browser_show(const char *root_path)
{
    char path_stack[8][512];
    int  depth = 0;
    strlcpy(path_stack[0], root_path, sizeof(path_stack[0]));

    load_dir(path_stack[depth]);

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

        if (key == 0) {
            // Arrow keys via special codes
            if (kb.key.key.opt_key.fn) {
                if (kb.key.key.key_data.keys[0] == 'i') { // up
                    if (s_cursor > 0) {
                        s_cursor--;
                        if (s_cursor < s_scroll) s_scroll = s_cursor;
                    }
                } else if (kb.key.key.key_data.keys[0] == 'k') { // down
                    if (s_cursor < s_entry_count - 1) {
                        s_cursor++;
                        int visible = (M5.Display.height() - 14) / LINE_HEIGHT;
                        if (s_cursor >= s_scroll + visible)
                            s_scroll = s_cursor - visible + 1;
                    }
                }
            }
        } else if (key == '\n' || key == '\r') {
            // Enter: open directory
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
        } else if (key == 127 || key == 8) {
            // Delete selected file
            if (s_cursor < s_entry_count && !s_entries[s_cursor].is_dir) {
                char full[512];
                snprintf(full, sizeof(full), "%s/%s",
                         path_stack[depth], s_entries[s_cursor].name);
                remove(full);
                load_dir(path_stack[depth]);
            }
        } else if (key == 27) {
            // ESC: go up
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
