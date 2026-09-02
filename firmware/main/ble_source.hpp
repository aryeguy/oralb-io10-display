#pragma once

#include "brush_snapshot.hpp"
#include "oralb_adv_parser.hpp"

#include "NimBLEDevice.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

class BleBrushSource : public NimBLEScanCallbacks {
public:
    explicit BleBrushSource(QueueHandle_t outputQueue);

    bool begin(uint32_t scanWindowMs);

private:
    QueueHandle_t queue_;
    uint32_t scanWindowMs_;
    OralBAdvParser parser_;

    void onResult(const NimBLEAdvertisedDevice* device) override;
    void onScanEnd(const NimBLEScanResults& results, int reason) override;

    bool isOralB(const NimBLEAdvertisedDevice* device) const;
    void handleManufacturerData(const NimBLEAdvertisedDevice* device);
};
