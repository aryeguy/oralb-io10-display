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
#include <cstdio>
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

bool MacStateClient::begin(DebugState* debug) {
    debug_ = debug;
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
    staNetif_ = esp_netif_create_default_wifi_sta();
    if (!staNetif_) {
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
    if (debug_) debug_->serverReachable = false;
    ESP_LOGI(TAG, "Connecting to Wi-Fi network '%s'", ORALB_WIFI_SSID);
    return true;
}

bool MacStateClient::update(uint32_t nowMs, BrushSnapshot& snapshot) {
    if (nowMs - lastPollMs_ < 500) return false;
    lastPollMs_ = nowMs;
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        if (debug_) debug_->wifiConnected = false;
        if (connected_) ESP_LOGW(TAG, "Wi-Fi disconnected");
        connected_ = false;
        esp_wifi_connect();
        return false;
    }
    if (debug_) {
        debug_->wifiConnected = true;
        esp_netif_ip_info_t ipInfo{};
        if (staNetif_ && esp_netif_get_ip_info(staNetif_, &ipInfo) == ESP_OK) {
            std::snprintf(
                debug_->ipAddress,
                sizeof(debug_->ipAddress),
                IPSTR,
                IP2STR(&ipInfo.ip)
            );
        }
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
    if (!client) {
        if (debug_) debug_->serverReachable = false;
        return false;
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (debug_) {
        debug_->lastHttpStatus = status;
        debug_->serverReachable = err == ESP_OK && status == 200;
    }
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
    if (debug_) {
        debug_->brushBrushing = snapshot.brushing;
        debug_->elapsedSeconds = snapshot.elapsedSeconds;
        cJSON* serverMotion = cJSON_GetObjectItem(brush, "motion_packet_count");
        if (cJSON_IsNumber(serverMotion)) {
            debug_->serverMotionPackets = static_cast<uint32_t>(serverMotion->valuedouble);
        }
        const char* classifier = cJSON_GetStringValue(cJSON_GetObjectItem(position, "status"));
        if (classifier) {
            std::strncpy(debug_->classifierStatus, classifier, sizeof(debug_->classifierStatus) - 1);
            debug_->classifierStatus[sizeof(debug_->classifierStatus) - 1] = '\0';
        }
    }
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
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        if (debug_) debug_->wifiConnected = false;
        return false;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "valid", snapshot.valid);
    cJSON_AddBoolToObject(root, "brushing", snapshot.brushing);
    cJSON_AddNumberToObject(root, "elapsed_seconds", snapshot.elapsedSeconds);
    cJSON_AddNumberToObject(root, "mode_raw", snapshot.modeRaw);
    cJSON_AddNumberToObject(root, "pacer_sector", snapshot.pacerSector);
    cJSON_AddNumberToObject(root, "pacer_sector_count", snapshot.pacerSectorCount);
    cJSON_AddNumberToObject(root, "pacer_sector_timer", snapshot.pacerSectorTimer);
    cJSON_AddStringToObject(root, "pressure", pressure_name(snapshot.pressure));
    if (snapshot.motionPayloadSize > 0) {
        static const char* hex = "0123456789abcdef";
        char encodedMotion[129]{};
        for (size_t i = 0; i < snapshot.motionPayloadSize; ++i) {
            encodedMotion[i * 2] = hex[(snapshot.motionPayload[i] >> 4) & 0x0f];
            encodedMotion[i * 2 + 1] = hex[snapshot.motionPayload[i] & 0x0f];
        }
        cJSON_AddStringToObject(root, "motion_payload_hex", encodedMotion);
    }
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
        if (debug_) debug_->serverReachable = false;
        cJSON_free(encoded);
        return false;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, encoded, std::strlen(encoded));
    esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    cJSON_free(encoded);
    if (debug_) {
        debug_->lastHttpStatus = status;
        if (err == ESP_OK && status == 200) debug_->serverReachable = true;
    }
    return err == ESP_OK && status == 200;
}
