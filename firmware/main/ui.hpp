#pragma once

#include "brush_state.hpp"

#include "lvgl.h"

#include <array>

class BrushUi {
public:
    bool begin();
    void render(const BrushState& state);

private:
    lv_obj_t* screen_ = nullptr;
    lv_obj_t* timer_ = nullptr;
    lv_obj_t* mode_ = nullptr;
    lv_obj_t* battery_ = nullptr;
    lv_obj_t* pressureDot_ = nullptr;
    lv_obj_t* pressureLabel_ = nullptr;
    lv_obj_t* detailLabel_ = nullptr;
    lv_obj_t* coverageLabel_ = nullptr;

    std::array<lv_obj_t*, kMouthSurfaceCount> surfaces_{};

    void buildMouth();
    void makeCircle(size_t index, int x, int y, int size);
    void makeBar(size_t index, int x, int y, int w, int h);
    void styleSurface(lv_obj_t* obj, uint8_t coverage, bool active);
};
