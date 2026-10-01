#pragma once

#include "brush_snapshot.hpp"

#include <cstdint>

class MacStateClient {
public:
    bool begin();
    bool update(uint32_t nowMs, BrushSnapshot& snapshot);

private:
    bool connected_ = false;
    uint32_t lastPollMs_ = 0;
};
