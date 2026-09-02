#pragma once

#include "brush_snapshot.hpp"

class MockBrushSource {
public:
    void begin(uint32_t nowMs);
    bool update(uint32_t nowMs, BrushSnapshot& out);

private:
    uint32_t startedMs_ = 0;
    uint32_t lastEmitMs_ = 0;
};
