#include "ble_source.hpp"

#include "esp_log.h"
#include "esp_timer.h"

#include <algorithm>
#include <string>

static const char* TAG = "oralb_ble";

BleBrushSource::BleBrushSource(QueueHandle_t outputQueue)
    : queue_(outputQueue),
      scanWindowMs_(30000) {}

bool BleBrushSource::begin(uint32_t scanWindowMs) {
    scanWindowMs_ = scanWindowMs;

    if (!NimBLEDevice::init("iO Display")) {
        ESP_LOGE(TAG, "NimBLE initialization failed");
        return false;
    }

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(this, true);  // duplicates are required for live telemetry
    scan->setActiveScan(false);          // manufacturer data is already in advertisements
    scan->setMaxResults(0);              // callback-only; do not retain all nearby devices
    scan->setInterval(45);
    scan->setWindow(30);

    if (!scan->start(scanWindowMs_, false, true)) {
        ESP_LOGE(TAG, "BLE scan start failed");
        return false;
    }

    ESP_LOGI(TAG, "Passive Oral-B scan started");
    return true;
}

bool BleBrushSource::isOralB(const NimBLEAdvertisedDevice* device) const {
    if (device == nullptr) {
        return false;
    }

    if (device->haveName()) {
        const std::string name = device->getName();
        if (name.find("Oral-B") != std::string::npos ||
            name.find("Oral B") != std::string::npos) {
            return true;
        }
    }

    // Some advertising events may omit the local name, so manufacturer data
    // is the final authority.
    if (device->haveManufacturerData()) {
        const std::string md = device->getManufacturerData();
        if (md.size() >= 2 &&
            static_cast<uint8_t>(md[0]) == 0xDC &&
            static_cast<uint8_t>(md[1]) == 0x00) {
            return true;
        }
    }

    return false;
}

void BleBrushSource::handleManufacturerData(const NimBLEAdvertisedDevice* device) {
    const uint8_t count = device->getManufacturerDataCount();

    for (uint8_t i = 0; i < count; ++i) {
        const std::string md = device->getManufacturerData(i);
        if (md.empty()) {
            continue;
        }

        BrushSnapshot snapshot;
        const uint32_t nowMs =
            static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

        if (!parser_.parse(
                reinterpret_cast<const uint8_t*>(md.data()),
                md.size(),
                nowMs,
                snapshot)) {
            continue;
        }

        xQueueOverwrite(queue_, &snapshot);

        ESP_LOGD(
            TAG,
            "t=%lus brushing=%d pressure=%s pacer=%u/%u mode=%u",
            static_cast<unsigned long>(snapshot.elapsedSeconds),
            snapshot.brushing,
            pressure_name(snapshot.pressure),
            snapshot.pacerSector,
            snapshot.pacerSectorCount,
            snapshot.modeRaw
        );
    }
}

void BleBrushSource::onResult(const NimBLEAdvertisedDevice* device) {
    if (!isOralB(device)) {
        return;
    }
    handleManufacturerData(device);
}

void BleBrushSource::onScanEnd(const NimBLEScanResults&, int reason) {
    ESP_LOGI(TAG, "Scan ended (%d), restarting", reason);
    NimBLEDevice::getScan()->start(scanWindowMs_, false, true);
}
