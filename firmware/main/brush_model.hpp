#pragma once

#include "brush_snapshot.hpp"
#include "brush_state.hpp"

class BrushModel {
public:
    BrushModel();

    void reset();
    void apply(const BrushSnapshot& snapshot);
    void tick(uint32_t nowMs, bool demoCoverage);

    const BrushState& state() const { return state_; }

private:
    BrushState state_;
    uint32_t anchorSeconds_ = 0;
    uint32_t anchorMs_ = 0;

    void updateDemoCoverage();
};
