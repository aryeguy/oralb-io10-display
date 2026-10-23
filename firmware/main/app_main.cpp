#include "app_config.hpp"
#include "ble_source.hpp"
#include "brush_model.hpp"
#include "mock_source.hpp"
#include "ui.hpp"
#include "mac_state_client.hpp"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <memory>
#include <vector>

static const char* TAG = "oralb_app";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting Oral-B iO display");

    // FF0D carries two samples per notification at roughly 13 packets/sec.
    // A one-slot overwrite queue silently reduced this to the 2 Hz UI loop,
    // which made the 26 Hz recurrent model impossible to run correctly.
    QueueHandle_t queue = xQueueCreate(64, sizeof(BrushSnapshot));
    if (queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create state queue");
        return;
    }

    DebugState debug;
    BrushUi ui;
    if (!ui.begin(&debug)) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }

    BrushModel model;
    ui.render(model.state());

    std::unique_ptr<BleBrushSource> bleSource;
    MacStateClient macClient;
    MockBrushSource mockSource;

    uint32_t nowMs =
        static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

    if (app_config::kUseMacServer) {
        ESP_LOGI(TAG, "Using Mac server state source");
        macClient.begin(&debug);
        // Bluetooth remains local to the ESP32. The Pi only receives the
        // decoded advertisement state over Wi-Fi.
        bleSource = std::make_unique<BleBrushSource>(queue, &debug);
        if (!bleSource->begin(app_config::kScanWindowMs)) {
            ESP_LOGE(TAG, "ESP32 BLE source failed to start");
        }
    } else if (app_config::kUseMockBrush) {
        ESP_LOGI(TAG, "Using mock toothbrush source");
        mockSource.begin(nowMs);
    } else {
        ESP_LOGI(TAG, "Using passive BLE source");
        bleSource = std::make_unique<BleBrushSource>(queue, &debug);
        if (!bleSource->begin(app_config::kScanWindowMs)) {
            ESP_LOGE(TAG, "BLE source failed to start");
        }
    }

    TickType_t lastWake = xTaskGetTickCount();
    std::vector<BrushSnapshot> telemetryBatch;
    telemetryBatch.reserve(64);

    while (true) {
        nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

        if (app_config::kUseMacServer) {
            telemetryBatch.clear();
            BrushSnapshot localSnapshot;
            while (telemetryBatch.size() < 64 &&
                   xQueueReceive(queue, &localSnapshot, 0) == pdTRUE) {
                model.apply(localSnapshot);
                telemetryBatch.push_back(localSnapshot);
            }
            if (!telemetryBatch.empty()) {
                macClient.publish(telemetryBatch.data(), telemetryBatch.size());
            }
            BrushSnapshot snapshot;
            if (macClient.update(nowMs, snapshot)) model.apply(snapshot);
        } else if (app_config::kUseMockBrush) {
            BrushSnapshot snapshot;
            if (mockSource.update(nowMs, snapshot)) {
                model.apply(snapshot);
            }
        } else {
            BrushSnapshot snapshot;
            if (xQueueReceive(queue, &snapshot, 0) == pdTRUE) {
                model.apply(snapshot);
            }
        }

        if (ui.consumeResetRequest()) {
            if (app_config::kUseMacServer) macClient.reset(nowMs);
            model.reset();
            ui.resetVisualState();
        }

        model.tick(nowMs, app_config::kDemoCoverageFromTimer);
        ui.render(model.state());

        vTaskDelayUntil(
            &lastWake,
            pdMS_TO_TICKS(app_config::kUiRefreshMs)
        );
    }
}
