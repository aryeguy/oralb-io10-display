#include "ui.hpp"

#include "app_config.hpp"
#include "qspi_display.hpp"
#include "bsp/esp-bsp.h"
#include "esp_log.h"

#include <algorithm>
#include <array>
#include <cstdio>

static lv_color_t color_from_hex(uint32_t hex) {
    return lv_color_hex(hex);
}

static const char* TAG = "oralb_ui";

namespace {

constexpr int kMouthCanvasWidth = 368;
constexpr int kMouthCanvasHeight = 350;
constexpr int kMouthCanvasY = 88;

struct MouthPoint {
    int16_t x;
    int16_t y;
};

struct MouthCurve {
    MouthPoint start;
    MouthPoint control1;
    MouthPoint control2;
    MouthPoint end;
};

// Six symbolic mouth groups, matching the browser preview. Corner groups have
// three concentric surfaces; the upper/lower middle groups have two. These
// cubic curves use the simulator's 340x350 coordinate space and are centered
// in the 368px hardware canvas at draw time.
constexpr std::array<MouthCurve, kMouthSurfaceCount> kMouthCurves{{
    {{24, 157}, {24, 101}, {50, 61}, {98, 36}},
    {{50, 158}, {50, 117}, {70, 87}, {102, 69}},
    {{76, 159}, {76, 136}, {86, 118}, {104, 105}},
    {{128, 34}, {156, 23}, {184, 23}, {212, 34}},
    {{128, 102}, {156, 92}, {184, 92}, {212, 102}},
    {{236, 105}, {254, 118}, {264, 136}, {264, 159}},
    {{238, 69}, {270, 87}, {290, 117}, {290, 158}},
    {{242, 36}, {290, 61}, {316, 101}, {316, 157}},
    {{24, 193}, {24, 249}, {50, 289}, {98, 314}},
    {{50, 192}, {50, 233}, {70, 263}, {102, 281}},
    {{76, 191}, {76, 214}, {86, 232}, {104, 245}},
    {{128, 316}, {156, 327}, {184, 327}, {212, 316}},
    {{128, 248}, {156, 258}, {184, 258}, {212, 248}},
    {{236, 245}, {254, 232}, {264, 214}, {264, 191}},
    {{238, 281}, {270, 263}, {290, 233}, {290, 192}},
    {{242, 314}, {290, 289}, {316, 249}, {316, 193}},
}};

uint32_t mix_hex(uint32_t from, uint32_t to, uint8_t amount) {
    const uint32_t inv = 255U - amount;
    const uint32_t r = (((from >> 16) & 0xffU) * inv + ((to >> 16) & 0xffU) * amount) / 255U;
    const uint32_t g = (((from >> 8) & 0xffU) * inv + ((to >> 8) & 0xffU) * amount) / 255U;
    const uint32_t b = ((from & 0xffU) * inv + (to & 0xffU) * amount) / 255U;
    return (r << 16) | (g << 8) | b;
}

uint32_t coverage_color(uint8_t coverage) {
    if (coverage == 0) return 0x17394E;
    if (coverage < 60) {
        const uint8_t amount = static_cast<uint8_t>((coverage - 1U) * 255U / 58U);
        return mix_hex(0xE9A23B, 0x3BADD8, amount);
    }
    if (coverage < 100) {
        const uint8_t amount = static_cast<uint8_t>((coverage - 60U) * 255U / 39U);
        return mix_hex(0x3BADD8, 0x35C890, amount);
    }
    return 0x35C890;
}

template <size_t SampleCount>
void calculate_curve_points(
    const MouthCurve& curve,
    std::array<lv_point_precise_t, SampleCount>& points
) {
    constexpr int32_t kXOffset = 14;
    constexpr int32_t kYOffset = -11;

    for (size_t i = 0; i < points.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(points.size() - 1);
        const float u = 1.0f - t;
        const float a = u * u * u;
        const float b = 3.0f * u * u * t;
        const float c = 3.0f * u * t * t;
        const float d = t * t * t;
        points[i].x = static_cast<int32_t>(
            a * curve.start.x + b * curve.control1.x +
            c * curve.control2.x + d * curve.end.x + kXOffset + 0.5f
        );
        points[i].y = static_cast<int32_t>(
            a * curve.start.y + b * curve.control1.y +
            c * curve.control2.y + d * curve.end.y + kYOffset + 0.5f
        );
    }

}

}  // namespace

bool BrushUi::begin(DebugState* debug) {
    debug_ = debug;
    lv_display_t* display = start_qspi_display();
    if (display == nullptr) {
        return false;
    }

    bsp_display_brightness_set(app_config::kDisplayBrightnessPercent);
    // start_qspi_display() enables the panel; set its AMOLED brightness here.

    if (!bsp_display_lock(1000)) {
        ESP_LOGE(TAG, "Timed out waiting for LVGL display lock");
        return false;
    }
    ESP_LOGI(TAG, "Building display UI");

    screen_ = lv_screen_active();
    lv_obj_set_scrollable(screen_, false);
    lv_display_add_event_cb(display, [](lv_event_t* event) {
        auto* debug = static_cast<DebugState*>(lv_event_get_user_data(event));
        if (debug) debug->refreshCount.fetch_add(1, std::memory_order_relaxed);
    }, LV_EVENT_REFR_READY, debug_);
    lv_obj_set_style_bg_color(screen_, color_from_hex(0x071827), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen_, color_from_hex(0xF4FAFF), 0);
    lv_obj_add_event_cb(screen_, wakeEvent, LV_EVENT_PRESSED, this);

    // Only the information that remains readable on a 1.8-inch display:
    // elapsed time, total coverage, the mouth map, and one pressure glyph.
    timer_ = lv_label_create(screen_);
    lv_obj_set_width(timer_, 225);
    lv_obj_set_pos(timer_, 16, 20);
    lv_obj_set_style_text_align(timer_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(timer_, &lv_font_montserrat_48, 0);
    lv_label_set_text(timer_, "00:00");

    coverageLabel_ = lv_label_create(screen_);
    lv_obj_set_width(coverageLabel_, 92);
    lv_obj_set_pos(coverageLabel_, 260, 29);
    lv_obj_set_style_text_align(coverageLabel_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(coverageLabel_, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(coverageLabel_, color_from_hex(0xDFF7EE), 0);
    lv_label_set_text(coverageLabel_, "0%");

    coverageBar_ = lv_obj_create(screen_);
    lv_obj_set_size(coverageBar_, 80, 7);
    lv_obj_set_pos(coverageBar_, 274, 68);
    lv_obj_set_style_radius(coverageBar_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(coverageBar_, 0, 0);
    lv_obj_set_style_pad_all(coverageBar_, 0, 0);
    lv_obj_set_style_bg_color(coverageBar_, color_from_hex(0x183142), 0);

    coverageFill_ = lv_obj_create(coverageBar_);
    lv_obj_set_size(coverageFill_, 0, 7);
    lv_obj_set_pos(coverageFill_, 0, 0);
    lv_obj_set_style_radius(coverageFill_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(coverageFill_, 0, 0);
    lv_obj_set_style_pad_all(coverageFill_, 0, 0);
    lv_obj_set_style_bg_color(coverageFill_, color_from_hex(0x39DDA0), 0);

    buildMouth();

    pressureIndicator_ = lv_obj_create(screen_);
    lv_obj_set_scrollable(pressureIndicator_, false);
    lv_obj_set_size(pressureIndicator_, 64, 64);
    lv_obj_set_pos(pressureIndicator_, 152, 220);
    lv_obj_set_style_radius(pressureIndicator_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(pressureIndicator_, 2, 0);
    lv_obj_set_style_border_color(pressureIndicator_, color_from_hex(0x28485C), 0);
    lv_obj_set_style_bg_color(pressureIndicator_, color_from_hex(0x091A26), 0);
    lv_obj_set_style_pad_all(pressureIndicator_, 0, 0);
    lv_obj_add_event_cb(pressureIndicator_, debugEvent, LV_EVENT_PRESSED, this);

    pressureIcon_ = lv_label_create(pressureIndicator_);
    lv_obj_set_style_text_font(pressureIcon_, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(pressureIcon_, color_from_hex(0x718B9B), 0);
    lv_label_set_text(pressureIcon_, LV_SYMBOL_MINUS);
    lv_obj_center(pressureIcon_);

    // Full-screen, readable transport diagnostics. The panel is intentionally
    // available on the device itself, so USB serial logs are not required to
    // distinguish BLE, Wi-Fi, and classifier failures.
    debugOverlay_ = lv_obj_create(screen_);
    lv_obj_set_size(debugOverlay_, 348, 428);
    lv_obj_set_pos(debugOverlay_, 10, 10);
    lv_obj_set_style_radius(debugOverlay_, 18, 0);
    lv_obj_set_style_bg_color(debugOverlay_, color_from_hex(0x06131F), 0);
    lv_obj_set_style_bg_opa(debugOverlay_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(debugOverlay_, 2, 0);
    lv_obj_set_style_border_color(debugOverlay_, color_from_hex(0x2A5974), 0);
    debugText_ = lv_label_create(debugOverlay_);
    lv_obj_set_width(debugText_, 320);
    lv_obj_set_pos(debugText_, 14, 18);
    lv_obj_set_style_text_font(debugText_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(debugText_, color_from_hex(0xDCEEFF), 0);
    lv_label_set_text(debugText_, "TRANSPORT DEBUG\nWaiting for diagnostics...");

    debugCloseButton_ = lv_btn_create(debugOverlay_);
    lv_obj_set_size(debugCloseButton_, 100, 48);
    lv_obj_set_pos(debugCloseButton_, 232, 364);
    lv_obj_set_style_radius(debugCloseButton_, 22, 0);
    lv_obj_set_style_bg_color(debugCloseButton_, color_from_hex(0x17344A), 0);
    lv_obj_set_style_border_width(debugCloseButton_, 1, 0);
    lv_obj_set_style_border_color(debugCloseButton_, color_from_hex(0x2A5974), 0);
    lv_obj_add_event_cb(debugCloseButton_, debugEvent, LV_EVENT_PRESSED, this);
    lv_obj_t* closeLabel = lv_label_create(debugCloseButton_);
    lv_obj_center(closeLabel);
    lv_obj_set_style_text_font(closeLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(closeLabel, color_from_hex(0xB9D6E8), 0);
    lv_label_set_text(closeLabel, "CLOSE");
    lv_obj_add_flag(debugOverlay_, LV_OBJ_FLAG_HIDDEN);

    bsp_display_unlock();
    ESP_LOGI(TAG, "Display UI ready");
    return true;
}

void BrushUi::buildMouth() {
    // Persistent line objects avoid clearing a 368x350 canvas on every
    // classifier update. LVGL can redraw the curves and overlaid pressure
    // indicator together; actual panel behavior is checked on the device.
    mouthLayer_ = lv_obj_create(screen_);
    lv_obj_set_size(mouthLayer_, kMouthCanvasWidth, kMouthCanvasHeight);
    lv_obj_set_pos(mouthLayer_, 0, kMouthCanvasY);
    lv_obj_set_style_bg_opa(mouthLayer_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mouthLayer_, 0, 0);
    lv_obj_set_style_pad_all(mouthLayer_, 0, 0);
    lv_obj_remove_flag(mouthLayer_, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < kMouthSurfaceCount; ++i) {
        calculate_curve_points(kMouthCurves[i], mouthPoints_[i]);

        mouthOutlines_[i] = lv_line_create(mouthLayer_);
        lv_obj_set_size(mouthOutlines_[i], kMouthCanvasWidth, kMouthCanvasHeight);
        lv_obj_set_pos(mouthOutlines_[i], 0, 0);
        lv_line_set_points(
            mouthOutlines_[i], mouthPoints_[i].data(), mouthPoints_[i].size()
        );
        lv_obj_set_style_line_color(mouthOutlines_[i], color_from_hex(0x0A2332), 0);
        lv_obj_set_style_line_width(mouthOutlines_[i], 19, 0);
        lv_obj_set_style_line_rounded(mouthOutlines_[i], true, 0);

        mouthSegments_[i] = lv_line_create(mouthLayer_);
        lv_obj_set_size(mouthSegments_[i], kMouthCanvasWidth, kMouthCanvasHeight);
        lv_obj_set_pos(mouthSegments_[i], 0, 0);
        lv_line_set_points(
            mouthSegments_[i], mouthPoints_[i].data(), mouthPoints_[i].size()
        );
        lv_obj_set_style_line_color(
            mouthSegments_[i], color_from_hex(coverage_color(0)), 0
        );
        lv_obj_set_style_line_width(mouthSegments_[i], 15, 0);
        lv_obj_set_style_line_rounded(mouthSegments_[i], true, 0);
    }
}

void BrushUi::drawMouth(const BrushState& state, uint32_t pressureColor) {
    if (mouthLayer_ == nullptr) return;

    for (size_t i = 0; i < kMouthSurfaceCount; ++i) {
        uint32_t fill = coverage_color(state.coverage[i]);
        int32_t width = 15;
        if (state.brushing && state.activeSurface == static_cast<int8_t>(i)) {
            fill = state.coverage[i] >= 100 ? 0xFFD166 : pressureColor;
            width = 18;
        }
        lv_obj_set_style_line_width(mouthOutlines_[i], width + 4, 0);
        lv_obj_set_style_line_color(mouthSegments_[i], color_from_hex(fill), 0);
        lv_obj_set_style_line_width(mouthSegments_[i], width, 0);
    }
}

void BrushUi::debugEvent(lv_event_t* event) {
    auto* ui = static_cast<BrushUi*>(lv_event_get_user_data(event));
    if (ui == nullptr) return;
    if (!ui->displayAwake_.load()) {
        ui->wakeRequested_.store(true);
        return;
    }
    ui->toggleDebug();
}

void BrushUi::wakeEvent(lv_event_t* event) {
    auto* ui = static_cast<BrushUi*>(lv_event_get_user_data(event));
    if (ui != nullptr) ui->wakeRequested_.store(true);
}

void BrushUi::toggleDebug() {
    debugVisible_ = !debugVisible_;
    if (debugOverlay_ == nullptr) return;
    if (debugVisible_) {
        lv_obj_clear_flag(debugOverlay_, LV_OBJ_FLAG_HIDDEN);
        renderDebug();
    } else {
        lv_obj_add_flag(debugOverlay_, LV_OBJ_FLAG_HIDDEN);
    }
}

void BrushUi::renderDebug() {
    if (debugText_ == nullptr) return;
    if (debug_ == nullptr) {
        lv_label_set_text(debugText_, "TRANSPORT DEBUG\nNo diagnostics source");
        return;
    }

    char text[640];
    const char* wifi = debug_->wifiConnected ? "UP" : "DOWN";
    const char* server = debug_->serverReachable ? "REACHABLE" : "NO RESPONSE";
    const char* gatt = debug_->gattConnected ? "UP" : "DOWN";
    const char* motion = debug_->motionSubscribed ? "SUBSCRIBED" : "NOT SUBSCRIBED";
    std::snprintf(
        text,
        sizeof(text),
        "TRANSPORT DEBUG\n\n"
        "Wi-Fi       %s\n"
        "IP          %s\n"
        "Server      %s (HTTP %d)\n\n"
        "BLE         %s\n"
        "GATT        %s\n"
        "Motion      %s\n"
        "Brush       %s  %02lu:%02lu\n"
        "RSSI        %d dBm\n"
        "Advertisements %lu\n"
        "Motion packets %lu\n"
        "Queue drops    %lu\n"
        "Server motion  %lu\n"
        "Classifier  %s\n"
        "Last motion  %u bytes\n"
        "GATT error   %d\n"
        "Disconnect   %d",
        wifi,
        debug_->ipAddress,
        server,
        debug_->lastHttpStatus,
        debug_ble_state_name(debug_->bleState),
        gatt,
        motion,
        debug_->brushBrushing ? "ON" : "OFF",
        static_cast<unsigned long>(debug_->elapsedSeconds / 60),
        static_cast<unsigned long>(debug_->elapsedSeconds % 60),
        static_cast<int>(debug_->rssi),
        static_cast<unsigned long>(debug_->advertisementPackets),
        static_cast<unsigned long>(debug_->motionPackets),
        static_cast<unsigned long>(debug_->droppedQueuePackets),
        static_cast<unsigned long>(debug_->serverMotionPackets),
        debug_->classifierStatus,
        static_cast<unsigned>(debug_->lastMotionLength),
        debug_->lastGattError,
        debug_->lastDisconnectReason
    );
    lv_label_set_text(debugText_, text);
}

void BrushUi::resetVisualState() {
    mouthRendered_ = false;
    wakeRequested_.store(true);
}

void BrushUi::setDisplayBrightness(int brightnessPercent) {
    if (brightnessPercent == currentBrightness_) return;
    if (bsp_display_brightness_set(brightnessPercent) == ESP_OK) {
        currentBrightness_ = brightnessPercent;
        if (debug_) debug_->displayBrightness = brightnessPercent;
        displayAwake_.store(brightnessPercent > 0);
        ESP_LOGI(TAG, "Display brightness %d%%", brightnessPercent);
    }
}

void BrushUi::updateDisplayPower(const BrushState& state, uint32_t nowMs) {
    if (lastDisplayActivityMs_ == 0) lastDisplayActivityMs_ = nowMs;

    const bool requested = wakeRequested_.exchange(false);
    if (state.brushing || requested) lastDisplayActivityMs_ = nowMs;

    const uint32_t idleMs = nowMs - lastDisplayActivityMs_;
    if (state.brushing || idleMs < app_config::kDisplayDimAfterMs) {
        setDisplayBrightness(app_config::kDisplayBrightnessPercent);
    } else if (idleMs < app_config::kDisplayOffAfterMs) {
        setDisplayBrightness(app_config::kDisplayDimBrightnessPercent);
    } else {
        setDisplayBrightness(0);
    }
}

void BrushUi::render(const BrushState& state, uint32_t nowMs) {
    if (screen_ == nullptr) {
        return;
    }

    if (!bsp_display_lock(1000)) {
        if (debug_) debug_->renderLockFailures++;
        return;
    }

    updateDisplayPower(state, nowMs);

    char buf[48];

    std::snprintf(
        buf,
        sizeof(buf),
        "%02lu:%02lu",
        static_cast<unsigned long>(state.elapsedSeconds / 60),
        static_cast<unsigned long>(state.elapsedSeconds % 60)
    );
    lv_label_set_text(timer_, buf);

    uint32_t pressureColor = 0x718B9B;
    uint32_t pressureBackground = 0x091A26;
    uint32_t pressureBorder = 0x28485C;
    const char* pressureIcon = LV_SYMBOL_MINUS;

    if (state.brushing) {
        switch (state.pressure) {
            case Pressure::Low:
                pressureColor = 0xE8F4FF;
                pressureBackground = 0x287AC2;
                pressureBorder = 0x74B8FF;
                pressureIcon = LV_SYMBOL_UP;
                break;
            case Pressure::Normal:
                pressureColor = 0xEAFFF7;
                pressureBackground = 0x15996A;
                pressureBorder = 0x7AEBBF;
                pressureIcon = LV_SYMBOL_OK;
                break;
            case Pressure::High:
                pressureColor = 0xFFF0F1;
                pressureBackground = 0xD94859;
                pressureBorder = 0xFF929C;
                pressureIcon = LV_SYMBOL_DOWN;
                break;
            default:
                pressureIcon = "?";
                break;
        }
    }

    uint32_t total = 0;
    bool allComplete = true;
    for (size_t i = 0; i < kMouthSurfaceCount; ++i) {
        if (state.coverage[i] < 100) allComplete = false;
        total += state.coverage[i];
    }

    if (allComplete) {
        pressureColor = 0xEAFFF7;
        pressureBackground = 0x15996A;
        pressureBorder = 0x8DF0C8;
        pressureIcon = LV_SYMBOL_OK;
    }

    lv_obj_set_style_text_color(pressureIcon_, color_from_hex(pressureColor), 0);
    lv_label_set_text(pressureIcon_, pressureIcon);
    lv_obj_set_style_bg_color(pressureIndicator_, color_from_hex(pressureBackground), 0);
    lv_obj_set_style_border_color(pressureIndicator_, color_from_hex(pressureBorder), 0);

    // Pressure belongs to the center indicator. Active coverage is violet,
    // or gold when brushing an area that is already complete.
    uint32_t activeColor = 0x8B5CF6;
    const bool mouthChanged =
        !mouthRendered_ ||
        renderedBrushing_ != state.brushing ||
        renderedActiveSurface_ != state.activeSurface ||
        renderedActiveColor_ != activeColor ||
        renderedCoverage_ != state.coverage;
    if (mouthChanged) {
        drawMouth(state, activeColor);
        mouthRendered_ = true;
        renderedBrushing_ = state.brushing;
        renderedActiveSurface_ = state.activeSurface;
        renderedActiveColor_ = activeColor;
        renderedCoverage_ = state.coverage;
    }

    const uint32_t avg =
        total / static_cast<uint32_t>(kMouthSurfaceCount);

    std::snprintf(buf, sizeof(buf), "%lu%%", static_cast<unsigned long>(avg));
    lv_label_set_text(coverageLabel_, buf);
    lv_obj_set_width(coverageFill_, static_cast<int32_t>(avg * 80U / 100U));

    if (debugVisible_) renderDebug();

    if (debug_) {
        debug_->renderedAtMs = nowMs;
        debug_->renderCount++;
        debug_->renderedBrushing = state.brushing;
        debug_->renderedElapsed = state.elapsedSeconds;
    }

    bsp_display_unlock();
}
