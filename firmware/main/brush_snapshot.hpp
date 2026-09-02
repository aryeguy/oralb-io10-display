#pragma once

#include "brush_state.hpp"
#include <cstdint>

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
};
