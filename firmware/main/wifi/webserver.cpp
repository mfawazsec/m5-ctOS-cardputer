#include "webserver.h"
#include "settings/config.h"
#include "modules/registry.h"
#include "modules/loader.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "webserver";
static httpd_handle_t s_server = NULL;

static bool is_authed(httpd_req_t *req)
{
    // Check cookie "ctos_auth=<token>"
    char cookie[128] = {};
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) != ESP_OK)
        return false;
    // Very simple token check — production would use HMAC-signed token
    return strstr(cookie, "ctos_auth=ok") != NULL;
}

// ---- GET / ----
static esp_err_t handle_root(httpd_req_t *req)
{
    if (!is_authed(req)) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/auth");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    char body[1024];
    size_t free_heap  = esp_get_free_heap_size();
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    int    mod_count  = module_registry_count();

    snprintf(body, sizeof(body),
        "<!DOCTYPE html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>ctOS</title></head><body>"
        "<h2>m5-ctOS v2.0</h2>"
        "<p>Free heap: %zu KB</p>"
        "<p>PSRAM free: %zu KB</p>"
        "<p>Loaded modules: %d</p>"
        "<nav><a href=/modules>Modules</a> | "
        "<a href=/settings>Settings</a></nav>"
        "</body></html>",
        free_heap / 1024, free_psram / 1024, mod_count);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, body);
    return ESP_OK;
}

// ---- GET /auth  POST /auth ----
static esp_err_t handle_auth_get(httpd_req_t *req)
{
    const char *page =
        "<!DOCTYPE html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>ctOS - Auth</title></head><body>"
        "<h2>ctOS PIN</h2>"
        "<form method=POST action=/auth>"
        "<input type=password name=pin maxlength=4 autofocus placeholder='PIN'>"
        "<button type=submit>Enter</button>"
        "</form></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, page);
    return ESP_OK;
}

static esp_err_t handle_auth_post(httpd_req_t *req)
{
    char body[64] = {};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_OK;
    }

    // Parse pin= from URL-encoded body
    char pin[8] = {};
    char *pin_start = strstr(body, "pin=");
    if (pin_start) {
        strlcpy(pin, pin_start + 4, sizeof(pin));
        // Strip trailing & or \0
        char *amp = strchr(pin, '&');
        if (amp) *amp = '\0';
    }

    if (config_verify_pin(pin)) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Set-Cookie", "ctos_auth=ok; Path=/; HttpOnly");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
    } else {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/auth?err=1");
        httpd_resp_send(req, NULL, 0);
    }
    return ESP_OK;
}

// ---- GET /modules ----
static esp_err_t handle_modules(httpd_req_t *req)
{
    if (!is_authed(req)) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/auth");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    char buf[4096];
    int  off = 0;
    off += snprintf(buf + off, sizeof(buf) - off,
        "<!DOCTYPE html><html><head><meta charset=utf-8>"
        "<title>ctOS - Modules</title></head><body>"
        "<h2>Modules</h2>"
        "<table border=1><tr><th>ID</th><th>Name</th><th>Version</th><th>Status</th><th>Action</th></tr>");

    int count = module_registry_count();
    for (int i = 0; i < count; i++) {
        module_info_t info;
        if (module_registry_get(i, &info) == ESP_OK) {
            off += snprintf(buf + off, sizeof(buf) - off,
                "<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td>"
                "<td><form method=POST action=/modules/toggle>"
                "<input type=hidden name=id value='%s'>"
                "<button>%s</button></form></td></tr>",
                info.id, info.name, info.version,
                info.running ? "RUNNING" : "IDLE",
                info.id,
                info.running ? "Unload" : "Load");
        }
    }

    off += snprintf(buf + off, sizeof(buf) - off,
        "</table><br><a href=/>Back</a></body></html>");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, buf, off);
    return ESP_OK;
}

// ---- GET /settings ----
static esp_err_t handle_settings(httpd_req_t *req)
{
    if (!is_authed(req)) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/auth");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    char buf[2048];
    snprintf(buf, sizeof(buf),
        "<!DOCTYPE html><html><head><meta charset=utf-8>"
        "<title>ctOS - Settings</title></head><body>"
        "<h2>Settings</h2>"
        "<form method=POST action=/settings/save>"
        "<label>WiFi SSID: <input name=ssid value='%s'></label><br>"
        "<label>WiFi Password: <input type=password name=pass></label><br>"
        "<label>Brightness: <input type=number name=brightness min=0 max=255 value=%d></label><br>"
        "<label>New PIN: <input type=password name=pin maxlength=4></label><br>"
        "<button type=submit>Save</button></form>"
        "<hr><form method=POST action=/settings/reboot>"
        "<button>Reboot</button></form>"
        "<br><a href=/>Back</a></body></html>",
        config_get_wifi_ssid(), config_get_brightness());

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

// ---- POST /modules/toggle ----
static esp_err_t handle_modules_toggle(httpd_req_t *req)
{
    if (!is_authed(req)) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/auth");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    char body[64] = {};
    httpd_req_recv(req, body, sizeof(body) - 1);

    char id[MODULE_ID_MAX_LEN] = {};
    char *p = strstr(body, "id=");
    if (p) {
        strlcpy(id, p + 3, sizeof(id));
        char *amp = strchr(id, '&');
        if (amp) *amp = '\0';
    }
    if (id[0]) {
        if (module_registry_is_running(id))
            module_loader_stop(id);
        else
            module_loader_start(id);
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/modules");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ---- POST /settings/save ----
static esp_err_t handle_settings_save(httpd_req_t *req)
{
    if (!is_authed(req)) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/auth");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    char body[512] = {};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_OK;
    }

    auto get_field = [](const char *src, const char *key, char *out, size_t out_sz) {
        char needle[64];
        snprintf(needle, sizeof(needle), "%s=", key);
        const char *p = strstr(src, needle);
        if (!p) { out[0] = '\0'; return; }
        p += strlen(needle);
        strlcpy(out, p, out_sz);
        char *amp = strchr(out, '&');
        if (amp) *amp = '\0';
    };

    char ssid[33] = {}, pass[64] = {}, brightness_s[8] = {}, pin[8] = {};
    get_field(body, "ssid",       ssid,         sizeof(ssid));
    get_field(body, "pass",       pass,         sizeof(pass));
    get_field(body, "brightness", brightness_s, sizeof(brightness_s));
    get_field(body, "pin",        pin,          sizeof(pin));

    if (ssid[0])                           config_set_wifi_ssid(ssid);
    if (pass[0])                           config_set_wifi_password(pass);
    if (brightness_s[0])                   config_set_brightness((uint8_t)atoi(brightness_s));
    if (pin[0] && strlen(pin) == 4)        config_set_pin(pin);

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/settings");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ---- POST /settings/reboot ----
static esp_err_t handle_settings_reboot(httpd_req_t *req)
{
    if (!is_authed(req)) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/auth");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, "<html><body><p>Rebooting...</p></body></html>");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK;
}

static const httpd_uri_t uri_table[] = {
    { .uri = "/",               .method = HTTP_GET,  .handler = handle_root },
    { .uri = "/auth",           .method = HTTP_GET,  .handler = handle_auth_get },
    { .uri = "/auth",           .method = HTTP_POST, .handler = handle_auth_post },
    { .uri = "/modules",        .method = HTTP_GET,  .handler = handle_modules },
    { .uri = "/modules/toggle", .method = HTTP_POST, .handler = handle_modules_toggle },
    { .uri = "/settings",       .method = HTTP_GET,  .handler = handle_settings },
    { .uri = "/settings/save",  .method = HTTP_POST, .handler = handle_settings_save },
    { .uri = "/settings/reboot",.method = HTTP_POST, .handler = handle_settings_reboot },
};

void webserver_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size        = 8192;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    for (size_t i = 0; i < sizeof(uri_table) / sizeof(uri_table[0]); i++)
        httpd_register_uri_handler(s_server, &uri_table[i]);

    ESP_LOGI(TAG, "Web server started on 192.168.4.1:80");
}

void webserver_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
