#include "mac_state_client.hpp"

#include "app_config.hpp"
#include "wifi_secrets.hpp"

#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include <cstdlib>
#include <cstring>
#include <string>

static const char* TAG = "mac_state";

static esp_err_t http_event(esp_http_client_event_t* event) {
    if (event->event_id == HTTP_EVENT_ON_DATA && event->user_data && event->data) {
        auto* body = static_cast<std::string*>(event->user_data);
        body->append(static_cast<const char*>(event->data), event->data_len);
    }
    return ESP_OK;
}

bool MacStateClient::begin() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "netif init failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop init failed: %s", esp_err_to_name(err));
        return false;
    }
    if (!esp_netif_create_default_wifi_sta()) {
        ESP_LOGE(TAG, "failed to create Wi-Fi station interface");
        return false;
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config{};
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), ORALB_WIFI_SSID,
                 sizeof(wifi_config.sta.ssid));
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password), ORALB_WIFI_PASSWORD,
                 sizeof(wifi_config.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
    ESP_LOGI(TAG, "Connecting to Wi-Fi network '%s'", ORALB_WIFI_SSID);
    return true;
}

bool MacStateClient::update(uint32_t nowMs, BrushSnapshot& snapshot) {
    if (nowMs - lastPollMs_ < 500) return false;
    lastPollMs_ = nowMs;
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        if (connected_) ESP_LOGW(TAG, "Wi-Fi disconnected");
        connected_ = false;
        esp_wifi_connect();
        return false;
    }
    connected_ = true;
    std::string body;
    esp_http_client_config_t config{};
    config.url = app_config::kMacServerUrl;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 350;
    config.buffer_size = 4096;
    config.buffer_size_tx = 512;
    config.keep_alive_enable = false;
    config.event_handler = http_event;
    config.user_data = &body;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status != 200) return false;

    cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
    if (!root) return false;
    cJSON* brush = cJSON_GetObjectItem(root, "brush");
    cJSON* position = brush ? cJSON_GetObjectItem(brush, "position") : nullptr;
    if (!brush || !position) { cJSON_Delete(root); return false; }
    auto number = [](cJSON* object, const char* key, int fallback) {
        cJSON* value = cJSON_GetObjectItem(object, key);
        return cJSON_IsNumber(value) ? value->valueint : fallback;
    };
    snapshot.valid = cJSON_IsTrue(cJSON_GetObjectItem(brush, "valid"));
    snapshot.brushing = cJSON_IsTrue(cJSON_GetObjectItem(brush, "brushing"));
    snapshot.elapsedSeconds = static_cast<uint32_t>(number(brush, "elapsed_seconds", 0));
    snapshot.modeRaw = static_cast<uint8_t>(number(brush, "mode_raw", 0));
    snapshot.pacerSector = static_cast<uint8_t>(number(brush, "pacer_sector", 0));
    snapshot.pacerSectorCount = static_cast<uint8_t>(number(brush, "pacer_sector_count", 0));
    snapshot.pacerSectorTimer = static_cast<uint8_t>(number(brush, "pacer_sector_timer", 0));
    snapshot.receivedAtMs = nowMs;
    const char* pressure = cJSON_GetStringValue(cJSON_GetObjectItem(brush, "pressure"));
    snapshot.pressure = pressure && std::strcmp(pressure, "high") == 0 ? Pressure::High
        : pressure && std::strcmp(pressure, "low") == 0 ? Pressure::Low : Pressure::Normal;
    cJSON* active = cJSON_GetObjectItem(position, "active_surface");
    snapshot.activeSurface = cJSON_IsNumber(active)
        ? static_cast<int8_t>(active->valueint - 1) : -1;
    cJSON* confidence = cJSON_GetObjectItem(position, "confidence");
    const double confidenceValue = cJSON_IsNumber(confidence) ? confidence->valuedouble : 0.0;
    snapshot.positionConfidence = static_cast<uint8_t>(confidenceValue * 100.0);
    cJSON* coverage = cJSON_GetObjectItem(position, "coverage");
    if (cJSON_IsArray(coverage)) {
        for (int i = 0; i < kMouthSurfaceCount; ++i) {
            cJSON* value = cJSON_GetArrayItem(coverage, i);
            if (cJSON_IsNumber(value)) {
                const double percent = value->valuedouble;
                snapshot.coverage[i] = static_cast<uint8_t>(percent < 0.0 ? 0.0 : percent > 100.0 ? 100.0 : percent);
            }
        }
    }
    cJSON_Delete(root);
    return true;
}

bool MacStateClient::reset(uint32_t nowMs) {
    (void)nowMs;
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return false;
    esp_http_client_config_t config{};
    config.url = app_config::kMacResetUrl;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 500;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return err == ESP_OK && status == 200;
}

bool MacStateClient::publish(const BrushSnapshot& snapshot) {
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return false;

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "valid", snapshot.valid);
    cJSON_AddBoolToObject(root, "brushing", snapshot.brushing);
    cJSON_AddNumberToObject(root, "elapsed_seconds", snapshot.elapsedSeconds);
    cJSON_AddNumberToObject(root, "mode_raw", snapshot.modeRaw);
    cJSON_AddNumberToObject(root, "pacer_sector", snapshot.pacerSector);
    cJSON_AddNumberToObject(root, "pacer_sector_count", snapshot.pacerSectorCount);
    cJSON_AddNumberToObject(root, "pacer_sector_timer", snapshot.pacerSectorTimer);
    cJSON_AddStringToObject(root, "pressure", pressure_name(snapshot.pressure));
    char* encoded = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!encoded) return false;

    esp_http_client_config_t config{};
    config.url = app_config::kMacIngestUrl;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 500;
    config.buffer_size = 1024;
    config.keep_alive_enable = false;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        cJSON_free(encoded);
        return false;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, encoded, std::strlen(encoded));
    esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    cJSON_free(encoded);
    return err == ESP_OK && status == 200;
}
