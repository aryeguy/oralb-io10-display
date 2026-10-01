#pragma once

#include "brush_state.hpp"
#include <cstdint>
#include <array>
#include <cstddef>

struct BrushSnapshot {
    bool valid = false;
    bool brushing = false;
    uint32_t elapsedSeconds = 0;
    Pressure pressure = Pressure::Unknown;
    uint8_t modeRaw = 0;
    uint8_t pacerSector = 0;
    uint8_t pacerSectorCount = 0;
    uint8_t pacerSectorTimer = 0;
    int batteryPercent = -1;
    uint32_t receivedAtMs = 0;
    std::array<uint8_t, kMouthSurfaceCount> coverage{};
    int8_t activeSurface = -1;
    uint8_t positionConfidence = 0;
    // Raw FF0D notification, relayed to the Pi where the motion parser and
    // position model run. Keeping the bytes here avoids coupling the ESP32
    // display firmware to the proprietary classifier.
    std::array<uint8_t, 64> motionPayload{};
    uint8_t motionPayloadSize = 0;
    uint32_t motionPacketCount = 0;
};
