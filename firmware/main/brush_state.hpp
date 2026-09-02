#pragma once

#include <array>
#include <cstdint>

enum class Pressure : uint8_t {
    Unknown = 0,
    Low,
    Normal,
    High,
};

enum class SurfaceKind : uint8_t {
    Outside,
    Chewing,
    Inside,
};

// This is the current *display geometry*, separate from Oral-B's timed
// pacer-sector count.
//
// It follows the simulator layout:
// 4 corner groups × 3 surfaces + 2 center groups × 2 surfaces = 16.
//
// A future validated motion classifier can map into these entries without
// changing the display or the rest of the state model.
constexpr size_t kMouthSurfaceCount = 16;

struct BrushState {
    bool seen = false;
    bool brushing = false;

    uint32_t elapsedSeconds = 0;
    uint32_t lastPacketMs = 0;

    Pressure pressure = Pressure::Unknown;
    uint8_t modeRaw = 0;
    const char* modeName = "Unknown";

    // Oral-B timed pacer values, not physical mouth position.
    uint8_t pacerSector = 0;
    uint8_t pacerSectorCount = 0;
    uint8_t pacerSectorTimer = 0;

    // Battery is not included in the normal advertisement payload.
    // It can be populated later from GATT.
    int batteryPercent = -1;

    // Physical coverage model. Real values are intentionally not synthesized
    // unless explicitly enabled in app_config.
    std::array<uint8_t, kMouthSurfaceCount> coverage{};
    int8_t activeSurface = -1;

    // FF0D carries inertial samples, not a ready-made mouth position.
    // A direct-GATT source can retain one raw payload here for research.
    std::array<uint8_t, 20> rawMotion{};
    uint8_t rawMotionLength = 0;
};

inline const char* pressure_name(Pressure p) {
    switch (p) {
        case Pressure::Low: return "LOW";
        case Pressure::Normal: return "GOOD";
        case Pressure::High: return "HIGH";
        default: return "--";
    }
}
