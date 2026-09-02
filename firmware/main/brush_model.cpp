#include "brush_model.hpp"

#include <algorithm>

BrushModel::BrushModel() {
    reset();
}

void BrushModel::reset() {
    state_ = BrushState{};
    anchorSeconds_ = 0;
    anchorMs_ = 0;
}

static const char* mode_name(uint8_t mode) {
    // iO-series advertisement mode mapping from oralb-ble.
    switch (mode) {
        case 0: return "DAILY CLEAN";
        case 1: return "SENSITIVE";
        case 2: return "GUM CARE";
        case 3: return "WHITEN";
        case 4: return "INTENSE";
        case 5: return "SUPER SENSITIVE";
        case 6: return "TONGUE";
        case 8: return "SETTINGS";
        case 9: return "OFF";
        case 11: return "SMART ADAPT";
        default: return "UNKNOWN";
    }
}

void BrushModel::apply(const BrushSnapshot& s) {
    if (!s.valid) {
        return;
    }

    state_.seen = true;
    state_.brushing = s.brushing;
    state_.elapsedSeconds = s.elapsedSeconds;
    state_.pressure = s.pressure;
    state_.modeRaw = s.modeRaw;
    state_.modeName = mode_name(s.modeRaw);
    state_.pacerSector = s.brushing ? s.pacerSector : 0;
    state_.pacerSectorCount = s.pacerSectorCount;
    state_.pacerSectorTimer = s.pacerSectorTimer;
    state_.lastPacketMs = s.receivedAtMs;

    if (s.batteryPercent >= 0) {
        state_.batteryPercent = s.batteryPercent;
    }

    anchorSeconds_ = s.elapsedSeconds;
    anchorMs_ = s.receivedAtMs;
}

void BrushModel::tick(uint32_t nowMs, bool demoCoverage) {
    // Smooth the timer locally between BLE packets, but always allow the next
    // authoritative packet to correct it.
    if (state_.brushing && anchorMs_ != 0 && nowMs >= anchorMs_) {
        state_.elapsedSeconds = anchorSeconds_ + ((nowMs - anchorMs_) / 1000);
    }

    if (demoCoverage) {
        updateDemoCoverage();
    }
}

void BrushModel::updateDemoCoverage() {
    constexpr uint32_t targetSeconds = 120;
    constexpr uint32_t count = static_cast<uint32_t>(kMouthSurfaceCount);

    const uint32_t clamped = std::min(state_.elapsedSeconds, targetSeconds);
    const uint32_t scaled = clamped * count * 100;

    for (size_t i = 0; i < kMouthSurfaceCount; ++i) {
        const int32_t value =
            static_cast<int32_t>(scaled / targetSeconds) -
            static_cast<int32_t>(i * 100);
        state_.coverage[i] =
            static_cast<uint8_t>(std::clamp<int32_t>(value, 0, 100));
    }

    const uint32_t idx = std::min<uint32_t>(
        (clamped * count) / targetSeconds,
        count - 1
    );
    state_.activeSurface = state_.brushing ? static_cast<int8_t>(idx) : -1;
}
