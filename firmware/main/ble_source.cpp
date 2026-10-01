#include "ble_source.hpp"

#include "esp_log.h"
#include "esp_timer.h"

#include <algorithm>
#include <cstring>
#include <string>

static const char* TAG = "oralb_ble";
static constexpr const char* kMotionUuid = "a0f0ff0d-5047-4d53-8208-4f72616c2d42";

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
        latestSnapshot_ = snapshot;

        // The manufacturer packet is also the discovery trigger for the
        // direct GATT session. FF0D is not present in advertisements.
        if (!client_ && !connecting_) {
            connectToBrush(device);
        }

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

void BleBrushSource::connectToBrush(const NimBLEAdvertisedDevice* device) {
    if (device == nullptr || connecting_ || client_ != nullptr) return;
    connecting_ = true;
    brushAddress_ = device->getAddress();
    ESP_LOGI(TAG, "Connecting to Oral-B GATT device %s", brushAddress_.toString().c_str());

    NimBLEDevice::getScan()->stop();
    client_ = NimBLEDevice::createClient();
    if (client_ == nullptr || !client_->connect(device)) {
        ESP_LOGW(TAG, "Oral-B GATT connection failed");
        if (client_) {
            NimBLEDevice::deleteClient(client_);
            client_ = nullptr;
        }
        connecting_ = false;
        NimBLEDevice::getScan()->start(scanWindowMs_, false, true);
        return;
    }

    NimBLERemoteService* service = client_->getService("a0f0ff00-5047-4d53-8208-4f72616c2d42");
    motionCharacteristic_ = service ? service->getCharacteristic(kMotionUuid) : nullptr;
    if (motionCharacteristic_ == nullptr || !motionCharacteristic_->canNotify()) {
        ESP_LOGW(TAG, "Oral-B GATT connected but FF0D notifications unavailable");
        client_->disconnect();
        NimBLEDevice::deleteClient(client_);
        client_ = nullptr;
        connecting_ = false;
        NimBLEDevice::getScan()->start(scanWindowMs_, false, true);
        return;
    }

    const bool subscribed = motionCharacteristic_->subscribe(
        true,
        [this](NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
            handleMotion(data, length);
        });
    if (!subscribed) {
        ESP_LOGW(TAG, "FF0D notification subscription failed");
        client_->disconnect();
        NimBLEDevice::deleteClient(client_);
        client_ = nullptr;
        connecting_ = false;
        NimBLEDevice::getScan()->start(scanWindowMs_, false, true);
        return;
    }

    connecting_ = false;
    ESP_LOGI(TAG, "Subscribed to Oral-B FF0D motion notifications");
}

void BleBrushSource::handleMotion(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) return;
    BrushSnapshot snapshot = latestSnapshot_;
    snapshot.valid = true;
    snapshot.motionPayloadSize = static_cast<uint8_t>(std::min(length, snapshot.motionPayload.size()));
    std::memcpy(snapshot.motionPayload.data(), data, snapshot.motionPayloadSize);
    snapshot.motionPacketCount = 1;
    snapshot.receivedAtMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    xQueueOverwrite(queue_, &snapshot);
    ESP_LOGI(TAG, "FF0D motion notification length=%u", static_cast<unsigned>(length));
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
