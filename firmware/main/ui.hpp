#pragma once

#include "brush_state.hpp"
#include "debug_state.hpp"

#include "lvgl.h"

#include <array>
#include <atomic>

class BrushUi {
public:
    bool begin(DebugState* debug = nullptr);
    void render(const BrushState& state, uint32_t nowMs);
    void resetVisualState();

private:
    lv_obj_t* screen_ = nullptr;
    lv_obj_t* timer_ = nullptr;
    lv_obj_t* coverageLabel_ = nullptr;
    lv_obj_t* coverageBar_ = nullptr;
    lv_obj_t* coverageFill_ = nullptr;
    lv_obj_t* mouthLayer_ = nullptr;
    static constexpr size_t kMouthCurveSamples = 13;
    std::array<std::array<lv_point_precise_t, kMouthCurveSamples>, kMouthSurfaceCount>
        mouthPoints_{};
    std::array<lv_obj_t*, kMouthSurfaceCount> mouthOutlines_{};
    std::array<lv_obj_t*, kMouthSurfaceCount> mouthSegments_{};
    lv_obj_t* pressureIndicator_ = nullptr;
    lv_obj_t* pressureIcon_ = nullptr;
    lv_obj_t* debugOverlay_ = nullptr;
    lv_obj_t* debugText_ = nullptr;
    lv_obj_t* debugCloseButton_ = nullptr;
    DebugState* debug_ = nullptr;

    bool debugVisible_ = false;
    std::atomic<bool> displayAwake_{true};
    std::atomic<bool> wakeRequested_{false};
    int currentBrightness_ = -1;
    uint32_t lastDisplayActivityMs_ = 0;
    bool mouthRendered_ = false;
    bool renderedBrushing_ = false;
    int8_t renderedActiveSurface_ = -2;
    uint32_t renderedActiveColor_ = 0;
    std::array<uint8_t, kMouthSurfaceCount> renderedCoverage_{};

    void buildMouth();
    void drawMouth(const BrushState& state, uint32_t pressureColor);
    void updateDisplayPower(const BrushState& state, uint32_t nowMs);
    void setDisplayBrightness(int brightnessPercent);
    static void debugEvent(lv_event_t* event);
    static void wakeEvent(lv_event_t* event);
    void toggleDebug();
    void renderDebug();
};
