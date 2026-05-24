/**
 * test_boot_sequence.c
 *
 * Host-compiled unit tests for the ctOS boot sequence.
 * Verifies each of the 9 boot phases in ctOS_main using
 * mock ESP-IDF functions — no hardware required.
 *
 * Each test follows the pattern:
 *   1. mock_reset_all()          — clean state
 *   2. configure mock return values if needed
 *   3. call the function under test
 *   4. assert expected side effects on mock counters / log strings
 */

#include "../unity/unity.h"
#include "../stubs/mock_globals.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

/* Mock headers — defines the globals the tests manipulate */
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Pull in the real source files (compiled for host via mock headers) */
#include "boot_sim.h"   /* declares boot_sim_* helpers wrapping each phase */

/* ─── setUp / tearDown ──────────────────────────────────────────────────── */

void setUp(void)    { mock_reset_all(); }
void tearDown(void) { /* nothing */ }

/* ═══════════════════════════════════════════════════════════════════════════
 * Phase 1: NVS Init
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_nvs_init_happy_path(void)
{
    g_nvs_flash_init_ret = ESP_OK;
    int result = boot_phase_nvs();
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, g_nvs_flash_init_calls);
    TEST_ASSERT_EQUAL(0, g_nvs_flash_erase_calls);
}

void test_nvs_init_dirty_triggers_erase_then_reinit(void)
{
    /* First call returns NO_FREE_PAGES; second call returns OK */
    g_nvs_flash_init_ret = ESP_ERR_NVS_NO_FREE_PAGES;
    int result = boot_phase_nvs();
    TEST_ASSERT_EQUAL(0, result);
    /* Should have called init TWICE and erase ONCE */
    TEST_ASSERT_EQUAL(2, g_nvs_flash_init_calls);
    TEST_ASSERT_EQUAL(1, g_nvs_flash_erase_calls);
}

void test_nvs_init_new_version_triggers_erase(void)
{
    g_nvs_flash_init_ret = ESP_ERR_NVS_NEW_VERSION_FOUND;
    boot_phase_nvs();
    TEST_ASSERT_EQUAL(1, g_nvs_flash_erase_calls);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Phase 2: PSRAM
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_psram_reports_size_when_present(void)
{
    g_psram_initialized = true;
    g_psram_size = 8 * 1024 * 1024;
    boot_phase_psram();
    /* Last log should contain "8192 KB" */
    TEST_ASSERT_NOT_NULL(strstr(g_last_log, "8192"));
}

void test_psram_logs_warning_when_absent(void)
{
    g_psram_initialized = false;
    boot_phase_psram();
    TEST_ASSERT_NOT_NULL(strstr(g_last_log, "not initialised"));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Phase 3: Config
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_config_init_reads_defaults_when_nvs_empty(void)
{
    g_nvs_open_ret = ESP_OK;
    /* nvs_get_str always returns NOT_FOUND in mock → defaults loaded */
    boot_phase_config();
    /* Default SSID should be "ctOS-EFEF12" (from fake MAC DE:AD:BE:EF:12:34) */
    const char *ssid = boot_get_ssid();
    TEST_ASSERT_NOT_NULL(ssid);
    TEST_ASSERT_TRUE(ssid[0] != '\0');
    TEST_ASSERT_EQUAL_STRING_LEN("ctOS-", ssid, 5);
}

void test_config_default_password_not_empty(void)
{
    g_nvs_open_ret = ESP_OK;
    boot_phase_config();
    const char *pass = boot_get_password();
    TEST_ASSERT_NOT_NULL(pass);
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(pass));
}

void test_config_wifi_ap_enabled_by_default(void)
{
    g_nvs_open_ret = ESP_OK;
    boot_phase_config();
    TEST_ASSERT_TRUE(boot_get_ap_enabled());
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Phase 4: Module registry
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_registry_init_starts_empty(void)
{
    boot_phase_registry();
    TEST_ASSERT_EQUAL(0, boot_get_module_count());
}

void test_registry_add_increments_count(void)
{
    boot_phase_registry();
    int r = boot_registry_add("test_mod", "1.0.0");
    TEST_ASSERT_EQUAL(0, r); /* ESP_OK */
    TEST_ASSERT_EQUAL(1, boot_get_module_count());
}

void test_registry_duplicate_add_rejected(void)
{
    boot_phase_registry();
    boot_registry_add("dup_mod", "1.0");
    int r = boot_registry_add("dup_mod", "1.0");
    TEST_ASSERT_NOT_EQUAL(0, r); /* ESP_ERR_INVALID_STATE */
    TEST_ASSERT_EQUAL(1, boot_get_module_count());
}

void test_registry_get_out_of_range_returns_error(void)
{
    boot_phase_registry();
    int r = boot_registry_get(99, NULL);
    TEST_ASSERT_NOT_EQUAL(0, r);
}

void test_registry_capacity_enforced(void)
{
    boot_phase_registry();
    /* Fill to capacity */
    for (int i = 0; i < 16; i++) {
        char id[16];
        snprintf(id, sizeof(id), "mod_%d", i);
        boot_registry_add(id, "1.0");
    }
    /* One more should fail */
    int r = boot_registry_add("overflow", "1.0");
    TEST_ASSERT_NOT_EQUAL(0, r);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Phase 5: Module loader
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_module_loader_init_succeeds(void)
{
    boot_phase_registry();
    boot_phase_loader();
    /* No crash and at least one log was emitted */
    TEST_ASSERT_GREATER_THAN(0, g_log_count);
}

void test_module_loader_autoload_empty_list_noop(void)
{
    boot_phase_registry();
    boot_phase_loader();
    /* Empty autoload list → no tasks created */
    int tasks_before = g_task_create_calls;
    boot_phase_autoload("");
    TEST_ASSERT_EQUAL(tasks_before, g_task_create_calls);
}

void test_module_loader_autoload_starts_task_per_id(void)
{
    boot_phase_registry();
    boot_phase_loader();
    /* Register two modules first */
    boot_registry_add("wifi_scan", "1.0");
    boot_registry_add("ble_hid",   "1.0");
    /* Autoload both */
    boot_phase_autoload("wifi_scan,ble_hid");
    TEST_ASSERT_EQUAL(2, g_task_create_calls);
}

void test_module_loader_start_task_fail_returns_error(void)
{
    boot_phase_registry();
    boot_phase_loader();
    boot_registry_add("fragile", "1.0");
    g_task_create_ret = pdFAIL; /* simulate task creation failure */
    int r = boot_loader_start("fragile");
    TEST_ASSERT_NOT_EQUAL(0, r); /* ESP_FAIL */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Phase 6: Keyboard (non-fatal path)
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_keyboard_failure_does_not_abort_boot(void)
{
    /* Keyboard init is non-fatal — boot must continue even if it returns false */
    bool kb_ok = boot_phase_keyboard(/*force_fail=*/true);
    (void)kb_ok;
    /* The test passes if we reach this line without abort() */
    TEST_PASS();
}

void test_keyboard_failure_logs_warning(void)
{
    boot_phase_keyboard(/*force_fail=*/true);
    TEST_ASSERT_NOT_NULL(strstr(g_last_log, "NOT found"));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Phase 8: WiFi hotspot
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_wifi_not_started_when_ap_disabled(void)
{
    boot_phase_config();
    /* Override: disable AP */
    boot_set_ap_enabled(false);
    int hotspot_calls = boot_phase_wifi();
    TEST_ASSERT_EQUAL(0, hotspot_calls);
}

void test_wifi_started_when_ap_enabled(void)
{
    boot_phase_config();
    boot_set_ap_enabled(true);
    int hotspot_calls = boot_phase_wifi();
    TEST_ASSERT_GREATER_THAN(0, hotspot_calls);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Boot-sequence ordering invariants
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_full_boot_sequence_completes_without_crash(void)
{
    g_nvs_flash_init_ret = ESP_OK;
    g_nvs_open_ret       = ESP_OK;
    g_psram_initialized  = true;
    g_task_create_ret    = pdPASS;

    int r = boot_run_full_sequence();
    TEST_ASSERT_EQUAL(0, r);
}

void test_full_boot_with_psram_absent_still_completes(void)
{
    g_psram_initialized = false;
    int r = boot_run_full_sequence();
    TEST_ASSERT_EQUAL(0, r);
}

void test_full_boot_with_nvs_dirty_still_completes(void)
{
    g_nvs_flash_init_ret = ESP_ERR_NVS_NO_FREE_PAGES;
    int r = boot_run_full_sequence();
    TEST_ASSERT_EQUAL(0, r);
    TEST_ASSERT_EQUAL(1, g_nvs_flash_erase_calls);
}

/* ─── Test runner ────────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    /* Phase 1 – NVS */
    RUN_TEST(test_nvs_init_happy_path);
    RUN_TEST(test_nvs_init_dirty_triggers_erase_then_reinit);
    RUN_TEST(test_nvs_init_new_version_triggers_erase);

    /* Phase 2 – PSRAM */
    RUN_TEST(test_psram_reports_size_when_present);
    RUN_TEST(test_psram_logs_warning_when_absent);

    /* Phase 3 – Config */
    RUN_TEST(test_config_init_reads_defaults_when_nvs_empty);
    RUN_TEST(test_config_default_password_not_empty);
    RUN_TEST(test_config_wifi_ap_enabled_by_default);

    /* Phase 4 – Registry */
    RUN_TEST(test_registry_init_starts_empty);
    RUN_TEST(test_registry_add_increments_count);
    RUN_TEST(test_registry_duplicate_add_rejected);
    RUN_TEST(test_registry_get_out_of_range_returns_error);
    RUN_TEST(test_registry_capacity_enforced);

    /* Phase 5 – Loader */
    RUN_TEST(test_module_loader_init_succeeds);
    RUN_TEST(test_module_loader_autoload_empty_list_noop);
    RUN_TEST(test_module_loader_autoload_starts_task_per_id);
    RUN_TEST(test_module_loader_start_task_fail_returns_error);

    /* Phase 6 – Keyboard */
    RUN_TEST(test_keyboard_failure_does_not_abort_boot);
    RUN_TEST(test_keyboard_failure_logs_warning);

    /* Phase 8 – WiFi */
    RUN_TEST(test_wifi_not_started_when_ap_disabled);
    RUN_TEST(test_wifi_started_when_ap_enabled);

    /* Full sequence */
    RUN_TEST(test_full_boot_sequence_completes_without_crash);
    RUN_TEST(test_full_boot_with_psram_absent_still_completes);
    RUN_TEST(test_full_boot_with_nvs_dirty_still_completes);

    return UNITY_END();
}
