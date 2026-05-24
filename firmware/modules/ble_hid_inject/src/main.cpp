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
// BLE HID keyboard report map (standard boot keyboard)
// ---------------------------------------------------------------------------
static const uint8_t s_kbd_report_map[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    // Modifier keys
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0xE0,        //   Usage Minimum (224)
    0x29, 0xE7,        //   Usage Maximum (231)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    // Reserved byte
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant)
    // Keys (6-key rollover)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x73,        //   Logical Maximum (115)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x73,        //   Usage Maximum (115)
    0x81, 0x00,        //   Input (Data, Array)
    0xC0               // End Collection
};

// HID Information: HID v1.11, country 0, flags: RemoteWake | NormallyConnectable
static const uint8_t s_hid_info[4]  = { 0x11, 0x01, 0x00, 0x03 };
// Report Reference descriptor: report ID 0, type INPUT (1)
static const uint8_t s_report_ref[2] = { 0x00, 0x01 };
static uint8_t s_protocol_mode = 0x01;  // Report Protocol Mode

// ---------------------------------------------------------------------------
// Connection state
// ---------------------------------------------------------------------------
static volatile bool     s_connected  = false;
static volatile uint16_t s_conn_hdl   = BLE_HS_CONN_HANDLE_NONE;
static volatile bool     s_injecting  = false;
static uint16_t          s_report_val_hdl = 0;

// ---------------------------------------------------------------------------
// GATT UUIDs
// ---------------------------------------------------------------------------
static const ble_uuid16_t s_hid_svc_uuid        = BLE_UUID16_INIT(0x1812);
static const ble_uuid16_t s_hid_info_uuid       = BLE_UUID16_INIT(0x2A4A);
static const ble_uuid16_t s_report_map_uuid     = BLE_UUID16_INIT(0x2A4B);
static const ble_uuid16_t s_ctrl_point_uuid     = BLE_UUID16_INIT(0x2A4C);
static const ble_uuid16_t s_protocol_mode_uuid  = BLE_UUID16_INIT(0x2A4E);
static const ble_uuid16_t s_report_chr_uuid     = BLE_UUID16_INIT(0x2A4D);
static const ble_uuid16_t s_report_ref_uuid     = BLE_UUID16_INIT(0x2908);

// ---------------------------------------------------------------------------
// GATT access callbacks
// ---------------------------------------------------------------------------
static int hid_info_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *c, void *a)
{
    if (c->op == BLE_GATT_ACCESS_OP_READ_CHR)
        os_mbuf_append(c->om, s_hid_info, sizeof(s_hid_info));
    return 0;
}

static int report_map_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *c, void *a)
{
    if (c->op == BLE_GATT_ACCESS_OP_READ_CHR)
        os_mbuf_append(c->om, s_kbd_report_map, sizeof(s_kbd_report_map));
    return 0;
}

static int ctrl_point_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *c, void *a)
{
    return 0;  // write-only, ignore
}

static int protocol_mode_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *c, void *a)
{
    if (c->op == BLE_GATT_ACCESS_OP_READ_CHR)
        os_mbuf_append(c->om, &s_protocol_mode, 1);
    else if (c->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
        os_mbuf_copydata(c->om, 0, 1, &s_protocol_mode);
    return 0;
}

static int report_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *c, void *a)
{
    return 0;  // input report; host reads via notifications
}

static int report_ref_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *c, void *a)
{
    if (c->op == BLE_GATT_ACCESS_OP_READ_DSC)
        os_mbuf_append(c->om, s_report_ref, sizeof(s_report_ref));
    return 0;
}

// ---------------------------------------------------------------------------
// GATT service definition (all required HID characteristics)
// ---------------------------------------------------------------------------
static struct ble_gatt_dsc_def s_report_dscs[] = {
    {
        .uuid      = &s_report_ref_uuid.u,
        .att_flags = BLE_ATT_F_READ,
        .access_cb = report_ref_cb,
    },
    { 0 },
};

static const struct ble_gatt_chr_def s_hid_chrs[] = {
    {   // HID Information (required)
        .uuid      = &s_hid_info_uuid.u,
        .access_cb = hid_info_cb,
        .flags     = BLE_GATT_CHR_F_READ,
    },
    {   // Report Map (required)
        .uuid      = &s_report_map_uuid.u,
        .access_cb = report_map_cb,
        .flags     = BLE_GATT_CHR_F_READ,
    },
    {   // HID Control Point (required)
        .uuid      = &s_ctrl_point_uuid.u,
        .access_cb = ctrl_point_cb,
        .flags     = BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {   // Protocol Mode (required)
        .uuid      = &s_protocol_mode_uuid.u,
        .access_cb = protocol_mode_cb,
        .flags     = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {   // HID Input Report — keyboard (CCCD auto-added by NimBLE for NOTIFY)
        .uuid        = &s_report_chr_uuid.u,
        .access_cb   = report_cb,
        .descriptors = s_report_dscs,
        .flags       = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle  = &s_report_val_hdl,
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
    if (ev->type == BLE_GAP_EVENT_CONNECT) {
        if (ev->connect.status == 0) {
            s_connected = true;
            s_conn_hdl  = ev->connect.conn_handle;
            s_api->log(ID, "HID host connected");
        }
    }
    if (ev->type == BLE_GAP_EVENT_DISCONNECT) {
        s_connected = false;
        s_conn_hdl  = BLE_HS_CONN_HANDLE_NONE;
        s_api->log(ID, "Disconnected — re-advertising");
        struct ble_gap_adv_params ap = {};
        ap.conn_mode = BLE_GAP_CONN_MODE_UND;
        ap.disc_mode = BLE_GAP_DISC_MODE_GEN;
        ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &ap, gap_event_cb, NULL);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Keyboard injection
// ---------------------------------------------------------------------------
static void send_hid_report(uint8_t modifier, uint8_t keycode)
{
    if (!s_connected || s_report_val_hdl == 0) return;
    uint8_t report[8] = { modifier, 0, keycode, 0, 0, 0, 0, 0 };
    struct os_mbuf *om = ble_hs_mbuf_from_flat(report, sizeof(report));
    if (om) ble_gatts_notify_custom(s_conn_hdl, s_report_val_hdl, om);
    vTaskDelay(pdMS_TO_TICKS(8));
    // key release
    uint8_t release[8] = {};
    om = ble_hs_mbuf_from_flat(release, sizeof(release));
    if (om) ble_gatts_notify_custom(s_conn_hdl, s_report_val_hdl, om);
}

static void inject_string(const char *str)
{
    s_injecting = true;
    while (*str) {
        char c = *str++;
        uint8_t mod = 0, kc = 0;
        if      (c >= 'a' && c <= 'z') { kc = 0x04 + (c - 'a'); }
        else if (c >= 'A' && c <= 'Z') { kc = 0x04 + (c - 'A'); mod = 0x02; }
        else if (c >= '1' && c <= '9') { kc = 0x1E + (c - '1'); }
        else if (c == '0')              { kc = 0x27; }
        else if (c == ' ')              { kc = 0x2C; }
        else if (c == '\n')             { kc = 0x28; }
        else if (c == '\t')             { kc = 0x2B; }
        else if (c == '-')              { kc = 0x2D; }
        else if (c == '.')              { kc = 0x37; }
        else if (c == '/')              { kc = 0x38; }
        else if (c == ':')              { kc = 0x33; mod = 0x02; }
        else if (c == '!')              { kc = 0x1E; mod = 0x02; }
        else if (c == '@')              { kc = 0x1F; mod = 0x02; }
        else if (c == '#')              { kc = 0x20; mod = 0x02; }
        else if (c == '"')              { kc = 0x34; mod = 0x02; }
        else if (c == '?')              { kc = 0x38; mod = 0x02; }
        if (kc) send_hid_report(mod, kc);
        vTaskDelay(pdMS_TO_TICKS(22));
    }
    s_injecting = false;
    s_api->log(ID, "Injection complete");
}

// ---------------------------------------------------------------------------
// Built-in inject payloads
// ---------------------------------------------------------------------------
static const char *k_payloads[] = {
    "Hello from ctOS BLE HID!\n",
    "https://github.com\n",
    "ctOS v2.0 was here.\n",
};
static const char *k_payload_labels[] = {
    "Hello ctOS",
    "github.com",
    "ctOS tag",
};
static const int k_payload_count = 3;

// ---------------------------------------------------------------------------
// BLE HID task
// ---------------------------------------------------------------------------
static void ble_hid_task(void *arg)
{
    ble_nimble_ensure_started();

    ble_svc_gap_device_name_set("ctOS Keyboard");
    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_gatts_count_cfg(s_gatt_svcs);
    ble_gatts_add_svcs(s_gatt_svcs);

    // BLE advertising data: HID (0x1812) appearance=keyboard (0x03C1)
    uint8_t adv_data[] = {
        0x02, 0x01, 0x06,           // Flags: LE General, BR/EDR off
        0x03, 0x03, 0x12, 0x18,     // Complete 16-bit UUIDs: HID (0x1812)
        0x03, 0x19, 0xC1, 0x03,     // Appearance: keyboard (0x03C1)
        0x0F, 0x09,                 // Complete Local Name (len=15)
        'c','t','O','S',' ','K','e','y','b','o','a','r','d','\0'
    };
    ble_gap_adv_set_data(adv_data, sizeof(adv_data));

    struct ble_gap_adv_params ap = {};
    ap.conn_mode = BLE_GAP_CONN_MODE_UND;
    ap.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &ap, gap_event_cb, NULL);
    s_api->log(ID, "Advertising as 'ctOS Keyboard'");

    while (module_registry_is_running(ID)) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ble_gap_adv_stop();
    s_api->log(ID, "BLE HID stopped");
    module_registry_set_running(ID, false);
    vTaskDelete(nullptr);
}

extern "C" esp_err_t ble_hid_inject_main(const ctos_api_t *api)
{
    if (module_registry_is_running(ID)) return ESP_OK;
    s_api       = api;
    s_connected = false;
    s_injecting = false;
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
        M5.Display.printf("Status: %s", s_connected ? "CONNECTED" : "ADVERTISING");

        // Payload list
        for (int i = 0; i < k_payload_count; i++) {
            bool sel = (i == cursor);
            M5.Display.setTextColor(sel ? TFT_BLACK : TFT_WHITE,
                                    sel ? TFT_CYAN  : TFT_BLACK);
            M5.Display.setCursor(0, 26 + i * MOD_LH);
            M5.Display.printf("[%d] %s", i, k_payload_labels[i]);
        }

        M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5.Display.setCursor(0, M5.Display.height() - 22);
        M5.Display.printf("Pair: 'ctOS Keyboard' BLE");
        M5.Display.setCursor(0, M5.Display.height() - 10);
        M5.Display.print("[Ent]inject [,/.]nav [L]log [`]bk");

        if (!CardputerKb.isChange() || !CardputerKb.isPressed()) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        auto kb = CardputerKb.getState();
        char key = (char)kb.key.key.key_data.keys[0];

        if (key == '`' || key == 27) return;
        else if (key == 'l' || key == 'L') log_view = true;
        else if ((key == ',' || key == ';') && cursor > 0) cursor--;
        else if ((key == '.' || key == '/') && cursor < k_payload_count - 1) cursor++;
        else if ((key == '\n' || key == '\r') && s_connected && !s_injecting) {
            inject_string(k_payloads[cursor]);
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
