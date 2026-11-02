#include "app_config.hpp"
#include "ble_source.hpp"
#include "brush_model.hpp"
#include "mock_source.hpp"
#include "ui.hpp"
#include "mac_state_client.hpp"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <memory>
#include <vector>

static const char* TAG = "oralb_app";

namespace {

class HardwareResetButton {
public:
    bool begin() {
        gpio_config_t config = {};
        config.pin_bit_mask = 1ULL << GPIO_NUM_0;
        config.mode = GPIO_MODE_INPUT;
        config.pull_up_en = GPIO_PULLUP_ENABLE;
        config.pull_down_en = GPIO_PULLDOWN_DISABLE;
        config.intr_type = GPIO_INTR_DISABLE;
        enabled_ = gpio_config(&config) == ESP_OK;
        return enabled_;
    }

    bool update(uint32_t nowMs) {
        if (!enabled_) return false;
        const bool pressed = gpio_get_level(GPIO_NUM_0) == 0;
        if (pressed != rawPressed_) {
            rawPressed_ = pressed;
            changedAtMs_ = nowMs;
        }

        if (rawPressed_ != stablePressed_ && nowMs - changedAtMs_ >= kDebounceMs) {
            stablePressed_ = rawPressed_;
            if (stablePressed_) {
                pressedAtMs_ = nowMs;
                consumed_ = false;
            }
        }

        if (stablePressed_ && !consumed_ && nowMs - pressedAtMs_ >= kResetHoldMs) {
            consumed_ = true;
            return true;
        }
        return false;
    }

private:
    static constexpr uint32_t kDebounceMs = 35;
    static constexpr uint32_t kResetHoldMs = 800;
    bool rawPressed_ = false;
    bool stablePressed_ = false;
    bool consumed_ = false;
    bool enabled_ = false;
    uint32_t changedAtMs_ = 0;
    uint32_t pressedAtMs_ = 0;
};

}  // namespace

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
    // BrushUi owns the persistent geometry for all mouth curves. Keep it in
    // static storage rather than the small ESP-IDF main-task stack.
    static BrushUi ui;
    if (!ui.begin(&debug)) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }

    BrushModel model(app_config::kUseMacServer);
    uint32_t nowMs =
        static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    ui.render(model.state(), nowMs);

    HardwareResetButton resetButton;
    if (resetButton.begin()) {
        ESP_LOGI(TAG, "Hold BOOT for 0.8s to reset the brushing session");
    } else {
        ESP_LOGW(TAG, "Could not initialize the BOOT session-reset button");
    }

    std::unique_ptr<BleBrushSource> bleSource;
    MacStateClient macClient;
    MockBrushSource mockSource;

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

        if (resetButton.update(nowMs)) {
            ESP_LOGI(TAG, "Resetting brushing session");
            if (app_config::kUseMacServer) macClient.reset(nowMs);
            model.reset();
            ui.resetVisualState();
        }

        // HTTP may take seconds. Power management and render timestamps must
        // use the time after I/O, rather than the beginning of the iteration.
        nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
        debug.stackFreeBytes = uxTaskGetStackHighWaterMark(nullptr);
        model.tick(nowMs, app_config::kDemoCoverageFromTimer);
        ui.render(model.state(), nowMs);

        vTaskDelayUntil(
            &lastWake,
            pdMS_TO_TICKS(app_config::kUiRefreshMs)
        );
    }
}
