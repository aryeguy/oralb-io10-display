#include "app_config.hpp"
#include "ble_source.hpp"
#include "brush_model.hpp"
#include "mock_source.hpp"
#include "ui.hpp"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <memory>

static const char* TAG = "oralb_app";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting Oral-B iO display");

    QueueHandle_t queue = xQueueCreate(1, sizeof(BrushSnapshot));
    if (queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create state queue");
        return;
    }

    BrushUi ui;
    if (!ui.begin()) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }

    BrushModel model;
    ui.render(model.state());

    std::unique_ptr<BleBrushSource> bleSource;
    MockBrushSource mockSource;

    uint32_t nowMs =
        static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

    if (app_config::kUseMockBrush) {
        ESP_LOGI(TAG, "Using mock toothbrush source");
        mockSource.begin(nowMs);
    } else {
        ESP_LOGI(TAG, "Using passive BLE source");
        bleSource = std::make_unique<BleBrushSource>(queue);
        if (!bleSource->begin(app_config::kScanWindowMs)) {
            ESP_LOGE(TAG, "BLE source failed to start");
        }
    }

    TickType_t lastWake = xTaskGetTickCount();

    while (true) {
        nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

        if (app_config::kUseMockBrush) {
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

        model.tick(nowMs, app_config::kDemoCoverageFromTimer);
        ui.render(model.state());

        vTaskDelayUntil(
            &lastWake,
            pdMS_TO_TICKS(app_config::kUiRefreshMs)
        );
    }
}
