#pragma once

#include "brush_snapshot.hpp"
#include "debug_state.hpp"
#include "esp_netif.h"

#include <cstddef>
#include <cstdint>

class MacStateClient {
public:
    bool begin(DebugState* debug = nullptr);
    bool update(uint32_t nowMs, BrushSnapshot& snapshot);
    bool reset(uint32_t nowMs);
    bool publish(const BrushSnapshot* snapshots, size_t count);

private:
    DebugState* debug_ = nullptr;
    esp_netif_t* staNetif_ = nullptr;
    bool connected_ = false;
    uint32_t lastPollMs_ = 0;
};
