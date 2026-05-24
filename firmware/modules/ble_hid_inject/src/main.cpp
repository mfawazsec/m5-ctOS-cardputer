#include "module_api.h"
#include "esp_log.h"
#include "NimBLEDevice.h"
#include "NimBLEHIDDevice.h"
#include "HIDKeyboardTypes.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ble_hid";
static const ctos_api_t *s_api = nullptr;

static NimBLEHIDDevice     *s_hid    = nullptr;
static NimBLECharacteristic *s_input  = nullptr;
static bool                  s_connected = false;

// BLE server callbacks
class HIDServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *server) override {
        s_connected = true;
        s_api->log(TAG, "BLE HID host connected");
        s_api->display_print(TAG, "HID connected! Ready to inject.");
    }
    void onDisconnect(NimBLEServer *server) override {
        s_connected = false;
        s_api->display_print(TAG, "HID disconnected. Advertising...");
        server->startAdvertising();
    }
};

static void ble_send_key(uint8_t modifier, uint8_t keycode)
{
    if (!s_input || !s_connected) return;
    uint8_t report[8] = { modifier, 0, keycode, 0, 0, 0, 0, 0 };
    s_input->setValue(report, sizeof(report));
    s_input->notify();
    vTaskDelay(pdMS_TO_TICKS(5));
    uint8_t release[8] = {};
    s_input->setValue(release, sizeof(release));
    s_input->notify();
}

static void ble_type_string(const char *str)
{
    while (*str) {
        char c = *str++;
        uint8_t mod = 0, kc = 0;
        if (c >= 'a' && c <= 'z') { kc = 0x04 + (c - 'a'); }
        else if (c >= 'A' && c <= 'Z') { kc = 0x04 + (c - 'A'); mod = 0x02; }
        else if (c == ' ') { kc = 0x2C; }
        else if (c == '\n') { kc = 0x28; }
        if (kc) ble_send_key(mod, kc);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// Scan for nearby BLE devices and display them
static void scan_targets(void)
{
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);

    s_api->display_print(TAG, "Scanning BLE targets (5s)...");
    NimBLEScanResults results = scan->start(5, false);

    char buf[512];
    int  off = snprintf(buf, sizeof(buf), "Found %d devices:\n", results.getCount());
    for (int i = 0; i < results.getCount() && off < (int)sizeof(buf) - 60; i++) {
        const NimBLEAdvertisedDevice *dev = results.getDevice(i);
        off += snprintf(buf + off, sizeof(buf) - off, "  %s %s\n",
                        dev->getAddress().toString().c_str(),
                        dev->getName().c_str());
    }
    s_api->display_print(TAG, buf);
    scan->clearResults();
}

static void ble_hid_task(void *arg)
{
    scan_targets();

    // Initialize BLE HID keyboard
    NimBLEDevice::init("ctOS Keyboard");
    NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND);

    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new HIDServerCallbacks());

    s_hid = new NimBLEHIDDevice(server);
    s_input = s_hid->inputReport(1);

    s_hid->manufacturer()->setValue("M5Stack");
    s_hid->pnp(0x02, 0x045E, 0x07A5, 0x0111);  // Microsoft vendor spoof
    s_hid->hidInfo(0x00, 0x02);

    // HID report map: standard boot keyboard
    static const uint8_t report_map[] = {
        0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07,
        0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
        0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
        0x75, 0x08, 0x81, 0x01, 0x95, 0x06, 0x75, 0x08,
        0x15, 0x00, 0x25, 0x73, 0x05, 0x07, 0x19, 0x00,
        0x29, 0x73, 0x81, 0x00, 0xC0
    };
    s_hid->reportMap((uint8_t *)report_map, sizeof(report_map));
    s_hid->startServices();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(0x03C1);  // HID keyboard
    adv->addServiceUUID(s_hid->hidService()->getUUID());
    adv->start();

    s_api->display_print(TAG, "BLE HID advertising as 'ctOS Keyboard'");
    s_api->log(TAG, "Waiting for host to pair...");

    // Wait for connection then demo-inject
    while (!s_connected) vTaskDelay(pdMS_TO_TICKS(500));
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Demo: type a test string
    ble_type_string("Hello from ctOS BLE HID");
    ble_send_key(0, 0x28);  // ENTER

    while (true) vTaskDelay(pdMS_TO_TICKS(5000));
}

extern "C" esp_err_t module_main(const ctos_api_t *api)
{
    s_api = api;
    api->log(TAG, "BLE HID inject starting");
    xTaskCreate(ble_hid_task, TAG, 8192, nullptr, 5, nullptr);
    return ESP_OK;
}
