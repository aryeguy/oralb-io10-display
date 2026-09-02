#include "mock_source.hpp"

void MockBrushSource::begin(uint32_t nowMs) {
    startedMs_ = nowMs;
    lastEmitMs_ = 0;
}

bool MockBrushSource::update(uint32_t nowMs, BrushSnapshot& out) {
    if (nowMs - lastEmitMs_ < 500) {
        return false;
    }
    lastEmitMs_ = nowMs;

    const uint32_t sec = (nowMs - startedMs_) / 1000;
    const uint32_t sessionSec = sec % 130;

    out = BrushSnapshot{};
    out.valid = true;
    out.receivedAtMs = nowMs;
    out.elapsedSeconds = sessionSec > 120 ? 120 : sessionSec;
    out.brushing = sessionSec <= 120;
    out.modeRaw = 0;
    out.pacerSectorCount = 6;
    out.pacerSector = out.brushing
        ? static_cast<uint8_t>((out.elapsedSeconds / 20) + 1)
        : 0;
    if (out.pacerSector > 6) out.pacerSector = 6;
    out.pacerSectorTimer = static_cast<uint8_t>(out.elapsedSeconds % 20);
    out.batteryPercent = 87;

    const uint32_t p = sec % 20;
    if (p < 4) out.pressure = Pressure::Low;
    else if (p < 16) out.pressure = Pressure::Normal;
    else out.pressure = Pressure::High;

    return true;
}
