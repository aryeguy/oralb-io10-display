#pragma once

#include <cstdint>

// Transport diagnostics shown on the ESP32 display. These fields describe
// whether each link is alive, independently of classifier confidence.
struct DebugState {
    enum class BleState : uint8_t { Off = 0, Scanning, Connecting, Connected, Subscribed, Error };

    BleState bleState = BleState::Off;
    bool wifiConnected = false;
    bool serverReachable = false;
    bool gattConnected = false;
    bool motionSubscribed = false;
    bool brushBrushing = false;
    uint32_t advertisementPackets = 0;
    uint32_t motionPackets = 0;
    uint32_t droppedQueuePackets = 0;
    uint32_t serverMotionPackets = 0;
    uint32_t elapsedSeconds = 0;
    uint16_t lastMotionLength = 0;
    int16_t rssi = 0;
    int lastGattError = 0;
    int lastDisconnectReason = 0;
    int lastHttpStatus = 0;
    uint32_t lastAdvertisementMs = 0;
    uint32_t lastMotionMs = 0;
    char ipAddress[16] = "--";
    char classifierStatus[20] = "--";
};

inline const char* debug_ble_state_name(DebugState::BleState state) {
    switch (state) {
        case DebugState::BleState::Scanning: return "SCANNING";
        case DebugState::BleState::Connecting: return "CONNECTING";
        case DebugState::BleState::Connected: return "CONNECTED";
        case DebugState::BleState::Subscribed: return "MOTION OK";
        case DebugState::BleState::Error: return "ERROR";
        default: return "OFF";
    }
}
