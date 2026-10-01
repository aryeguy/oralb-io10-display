#pragma once

#include "brush_state.hpp"

#include "lvgl.h"

#include <array>

class BrushUi {
public:
    bool begin();
    void render(const BrushState& state);
    bool consumeResetRequest();
    void resetVisualState();

private:
    lv_obj_t* screen_ = nullptr;
    lv_obj_t* timer_ = nullptr;
    lv_obj_t* mode_ = nullptr;
    lv_obj_t* battery_ = nullptr;
    lv_obj_t* pressureDot_ = nullptr;
    lv_obj_t* pressureLabel_ = nullptr;
    lv_obj_t* detailLabel_ = nullptr;
    lv_obj_t* coverageLabel_ = nullptr;
    lv_obj_t* celebrationLabel_ = nullptr;
    lv_obj_t* resetButton_ = nullptr;
    lv_obj_t* resetButtonLabel_ = nullptr;

    std::array<lv_obj_t*, kMouthSurfaceCount> surfaces_{};
    std::array<uint8_t, kMouthSurfaceCount> previousCoverage_{};
    bool hasRendered_ = false;
    bool previousAllComplete_ = false;
    bool resetRequested_ = false;

    void buildMouth();
    void makeCircle(size_t index, int x, int y, int size);
    void makeBar(size_t index, int x, int y, int w, int h);
    void styleSurface(lv_obj_t* obj, uint8_t coverage, bool active);
    void pulseSurface(size_t index);
    void showCelebration(const char* text);
    static void borderAnimation(void* object, int32_t value);
    static void celebrationAnimation(void* object, int32_t value);
    static void resetEvent(lv_event_t* event);
};
