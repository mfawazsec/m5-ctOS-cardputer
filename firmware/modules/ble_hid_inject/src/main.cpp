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
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ble_hid";
static const char *ID  = "ble_hid_inject";
static const ctos_api_t *s_api = nullptr;

// ---------------------------------------------------------------------------
// BLE HID keyboard report map (boot keyboard)
// ---------------------------------------------------------------------------
static const uint8_t s_kbd_report_map[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x73,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x73, 0x81, 0x00, 0xC0
};

// ---------------------------------------------------------------------------
// Scan state
// ---------------------------------------------------------------------------
#define MAX_SCAN_DEVS 16
typedef struct {
    ble_addr_t addr;
    char       name[32];
    int8_t     rssi;
} scan_dev_t;

static scan_dev_t s_scan_devs[MAX_SCAN_DEVS];
static volatile int  s_scan_count    = 0;
static volatile bool s_scanning      = false;
static volatile bool s_connected     = false;
static volatile uint16_t s_conn_hdl  = BLE_HS_CONN_HANDLE_NONE;
static volatile bool s_injecting     = false;
static volatile uint16_t s_report_chr_hdl = 0;
static SemaphoreHandle_t s_event_sem;

// ---------------------------------------------------------------------------
// GATT server for HID service
// ---------------------------------------------------------------------------
static int hid_report_map_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
        os_mbuf_append(ctxt->om, s_kbd_report_map, sizeof(s_kbd_report_map));
    return 0;
}

static int hid_report_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return 0;  // input-only characteristic
}

// Report characteristic handle, filled after GATT service registration
static uint16_t s_report_val_hdl = 0;

static const ble_uuid16_t s_hid_svc_uuid      = BLE_UUID16_INIT(0x1812);
static const ble_uuid16_t s_report_map_uuid   = BLE_UUID16_INIT(0x2A4B);
static const ble_uuid16_t s_report_chr_uuid   = BLE_UUID16_INIT(0x2A4D);

static const struct ble_gatt_chr_def s_hid_chrs[] = {
    {   // Report Map
        .uuid       = &s_report_map_uuid.u,
        .access_cb  = hid_report_map_access_cb,
        .flags      = BLE_GATT_CHR_F_READ,
    },
    {   // HID Input Report (keyboard)
        .uuid       = &s_report_chr_uuid.u,
        .access_cb  = hid_report_access_cb,
        .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_report_val_hdl,
    },
    { 0 },
};

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type            = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid            = &s_hid_svc_uuid.u,
        .characteristics = s_hid_chrs,
    },
    { 0 },
};

// ---------------------------------------------------------------------------
// GAP event handler
// ---------------------------------------------------------------------------
static int gap_event_cb(struct ble_gap_event *ev, void *arg)
{
    if (ev->type == BLE_GAP_EVENT_DISC) {
        if (s_scan_count >= MAX_SCAN_DEVS) return 0;
        scan_dev_t *d = &s_scan_devs[s_scan_count];
        memcpy(&d->addr, &ev->disc.addr, sizeof(ble_addr_t));
        d->rssi = ev->disc.rssi;
        d->name[0] = '\0';
        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, ev->disc.data, ev->disc.length_data) == 0 && fields.name) {
            size_t n = fields.name_len < sizeof(d->name)-1 ? fields.name_len : sizeof(d->name)-1;
            memcpy(d->name, fields.name, n);
            d->name[n] = '\0';
        }
        s_scan_count++;
    }
    if (ev->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        s_scanning = false;
        xSemaphoreGive(s_event_sem);
    }
    if (ev->type == BLE_GAP_EVENT_CONNECT) {
        if (ev->connect.status == 0) {
            s_connected = true;
            s_conn_hdl  = ev->connect.conn_handle;
            s_api->log(ID, "HID host connected");
        }
        xSemaphoreGive(s_event_sem);
    }
    if (ev->type == BLE_GAP_EVENT_DISCONNECT) {
        s_connected = false;
        s_conn_hdl  = BLE_HS_CONN_HANDLE_NONE;
        s_api->log(ID, "HID host disconnected — re-advertising");
        // Re-advertise
        struct ble_gap_adv_params ap = {};
        ap.conn_mode = BLE_GAP_CONN_MODE_UND;
        ap.disc_mode = BLE_GAP_DISC_MODE_GEN;
        ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &ap, gap_event_cb, NULL);
    }
    return 0;
}

static void send_hid_report(uint8_t modifier, uint8_t keycode)
{
    if (!s_connected || s_report_val_hdl == 0) return;
    uint8_t report[8] = { modifier, 0, keycode, 0, 0, 0, 0, 0 };
    struct os_mbuf *om = ble_hs_mbuf_from_flat(report, sizeof(report));
    if (om) ble_gattc_notify_custom(s_conn_hdl, s_report_val_hdl, om);
    vTaskDelay(pdMS_TO_TICKS(5));
    // Key release
    uint8_t release[8] = {};
    om = ble_hs_mbuf_from_flat(release, sizeof(release));
    if (om) ble_gattc_notify_custom(s_conn_hdl, s_report_val_hdl, om);
}

static void inject_string(const char *str)
{
    s_injecting = true;
    while (*str) {
        char c = *str++;
        uint8_t mod = 0, kc = 0;
        if (c >= 'a' && c <= 'z')      { kc = 0x04 + (c-'a'); }
        else if (c >= 'A' && c <= 'Z') { kc = 0x04 + (c-'A'); mod = 0x02; }
        else if (c == ' ')              { kc = 0x2C; }
        else if (c == '\n')             { kc = 0x28; }
        if (kc) send_hid_report(mod, kc);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_injecting = false;
    s_api->log(ID, "Injection complete");
}

static void ble_hid_task(void *arg)
{
    ble_nimble_ensure_started();
    ble_svc_gap_device_name_set("ctOS Keyboard");
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(s_gatt_svcs);
    ble_gatts_add_svcs(s_gatt_svcs);

    // Advertise as HID keyboard
    uint8_t adv_data[] = {
        0x02, 0x01, 0x06,           // Flags: LE General Discoverable, BR/EDR Not Supported
        0x03, 0x03, 0x12, 0x18,     // Complete 16-bit UUIDs: HID (0x1812)
        0x0F, 0x09,                 // Complete Local Name length = 15
        'c','t','O','S',' ','K','e','y','b','o','a','r','d','\0' // name
    };
    ble_gap_adv_set_data(adv_data, sizeof(adv_data));

    struct ble_gap_adv_params ap = {};
    ap.conn_mode = BLE_GAP_CONN_MODE_UND;
    ap.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &ap, gap_event_cb, NULL);
    s_api->log(ID, "Advertising as 'ctOS Keyboard'");

    // Scan briefly to find nearby targets
    s_scan_count = 0;
    s_scanning   = true;
    struct ble_gap_disc_params dp = {};
    dp.filter_duplicates = 1; dp.passive = 0; dp.itvl = 80; dp.window = 40;
    ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 5000, &dp, gap_event_cb, NULL);
    xSemaphoreTake(s_event_sem, pdMS_TO_TICKS(7000));
    s_scanning = false;

    while (module_registry_is_running(ID)) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ble_gap_adv_stop();
    ble_gap_disc_cancel();
    s_api->log(ID, "BLE HID stopped");
    module_registry_set_running(ID, false);
    vTaskDelete(nullptr);
}

extern "C" esp_err_t ble_hid_inject_main(const ctos_api_t *api)
{
    if (module_registry_is_running(ID)) return ESP_OK;
    s_api       = api;
    s_scan_count = 0;
    s_connected  = false;
    s_injecting  = false;
    if (!s_event_sem) s_event_sem = xSemaphoreCreateBinary();
    module_registry_set_running(ID, true);
    if (xTaskCreate(ble_hid_task, TAG, 12288, nullptr, 5, nullptr) != pdPASS) {
        module_registry_set_running(ID, false);
        return ESP_FAIL;
    }
    return ESP_OK;
}

extern "C" void ble_hid_inject_ui_show(void)
{
    module_loader_start(ID);
    mod_drain_keys();
    bool log_view = false;
    int  cursor   = 0;

    while (true) {
        M5.update();
        CardputerKb.update();

        if (log_view) { mod_show_log_view(ID, "BLE HID"); log_view = false; mod_drain_keys(); continue; }

        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
        M5.Display.setCursor(0, 0); M5.Display.print("BLE HID INJECT");

        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.setCursor(0, 14);
        M5.Display.printf("Advert: %s", s_connected ? "CONNECTED" : "ADVERTISING");
        M5.Display.setCursor(0, 26);
        M5.Display.printf("Host:   %s", s_connected ? "PAIRED" : "WAITING...");
        M5.Display.setCursor(0, 38);
        M5.Display.printf("Scan:   %d targets found", s_scan_count);

        // Show up to 3 scanned targets
        for (int i = 0; i < 3 && i < s_scan_count; i++) {
            bool sel = (i == cursor);
            M5.Display.setTextColor(sel ? TFT_BLACK : TFT_WHITE,
                                    sel ? TFT_CYAN  : TFT_BLACK);
            M5.Display.setCursor(0, 50 + i * MOD_LH);
            M5.Display.printf("[%d] %02x:%02x:%02x %s", i,
                              s_scan_devs[i].addr.val[5], s_scan_devs[i].addr.val[4],
                              s_scan_devs[i].addr.val[3], s_scan_devs[i].name);
        }

        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.setCursor(0, M5.Display.height() - 10);
        M5.Display.print("[Ent]inject [,/.]nav [L]log [`]bk");

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        auto kb = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];

        if (key == '`' || key == 27) return;
        else if (key == 'l' || key == 'L') log_view = true;
        else if ((key == ',' || key == ';') && cursor > 0) cursor--;
        else if ((key == '.' || key == '/') && cursor < s_scan_count - 1) cursor++;
        else if ((key == '\n' || key == '\r') && s_connected && !s_injecting) {
            // Inject demo string
            inject_string("Hello from ctOS BLE HID\n");
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
