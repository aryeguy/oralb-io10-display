#pragma once

#include "brush_snapshot.hpp"
#include "debug_state.hpp"
#include "oralb_adv_parser.hpp"

#include "NimBLEDevice.h"
#include "NimBLEClient.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

class BleBrushSource : public NimBLEScanCallbacks {
public:
    explicit BleBrushSource(QueueHandle_t outputQueue, DebugState* debug = nullptr);

    bool begin(uint32_t scanWindowMs);
    void handleClientDisconnect(NimBLEClient* client, int reason);
    void handleClientConnect(NimBLEClient* client);
    void handleClientConnectFail(NimBLEClient* client, int reason);

private:
    QueueHandle_t queue_;
    DebugState* debug_ = nullptr;
    uint32_t scanWindowMs_;
    OralBAdvParser parser_;
    BrushSnapshot latestSnapshot_;
    NimBLEClient* client_ = nullptr;
    NimBLERemoteCharacteristic* motionCharacteristic_ = nullptr;
    bool connecting_ = false;
    NimBLEAddress brushAddress_;

    void onResult(const NimBLEAdvertisedDevice* device) override;
    void onScanEnd(const NimBLEScanResults& results, int reason) override;

    bool isOralB(const NimBLEAdvertisedDevice* device) const;
    void handleManufacturerData(const NimBLEAdvertisedDevice* device);
    void connectToBrush(const NimBLEAdvertisedDevice* device);
    void discoverAndSubscribe(NimBLEClient* client);
    void handleMotion(const uint8_t* data, size_t length);
};
