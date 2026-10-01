#include "ui.hpp"

#include "app_config.hpp"
#include "bsp/esp-bsp.h"
#include "esp_log.h"

#include <algorithm>
#include <cstdio>

static lv_color_t color_from_hex(uint32_t hex) {
    return lv_color_hex(hex);
}

static const char* TAG = "oralb_ui";

bool BrushUi::begin() {
    lv_display_t* display = bsp_display_start();
    if (display == nullptr) {
        return false;
    }

    bsp_display_brightness_set(app_config::kDisplayBrightnessPercent);
    // The BSP enables the panel/backlight as part of bsp_display_start().

    if (!bsp_display_lock(1000)) {
        ESP_LOGE(TAG, "Timed out waiting for LVGL display lock");
        return false;
    }
    ESP_LOGI(TAG, "Building display UI");

    screen_ = lv_screen_active();
    lv_obj_set_style_bg_color(screen_, color_from_hex(0x071827), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen_, color_from_hex(0xF4FAFF), 0);

    // Mode
    mode_ = lv_label_create(screen_);
    lv_obj_set_pos(mode_, 18, 14);
    lv_obj_set_style_text_font(mode_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(mode_, color_from_hex(0x8DA9C2), 0);
    lv_label_set_text(mode_, "DAILY CLEAN");

    // Battery
    battery_ = lv_label_create(screen_);
    lv_obj_set_pos(battery_, 300, 14);
    lv_obj_set_style_text_font(battery_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(battery_, color_from_hex(0x8DA9C2), 0);
    lv_label_set_text(battery_, "--");

    // Timer
    timer_ = lv_label_create(screen_);
    lv_obj_set_width(timer_, 368);
    lv_obj_set_pos(timer_, 0, 37);
    lv_obj_set_style_text_align(timer_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(timer_, &lv_font_montserrat_48, 0);
    lv_label_set_text(timer_, "00:00");

    lv_obj_t* goal = lv_label_create(screen_);
    lv_obj_set_width(goal, 368);
    lv_obj_set_pos(goal, 0, 93);
    lv_obj_set_style_text_align(goal, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(goal, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(goal, color_from_hex(0x6E8EA9), 0);
    lv_label_set_text(goal, "BRUSHING GOAL  2:00");

    buildMouth();

    celebrationLabel_ = lv_label_create(screen_);
    lv_obj_set_width(celebrationLabel_, 368);
    lv_obj_set_pos(celebrationLabel_, 0, 112);
    lv_obj_set_style_text_align(celebrationLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(celebrationLabel_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(celebrationLabel_, color_from_hex(0xFFD166), 0);
    lv_obj_set_style_opa(celebrationLabel_, LV_OPA_TRANSP, 0);
    lv_label_set_text(celebrationLabel_, "");

    // Bottom status and touch control
    pressureDot_ = lv_obj_create(screen_);
    lv_obj_set_size(pressureDot_, 18, 18);
    lv_obj_set_pos(pressureDot_, 18, 405);
    lv_obj_set_style_radius(pressureDot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(pressureDot_, 0, 0);
    lv_obj_set_style_bg_color(pressureDot_, color_from_hex(0x66788A), 0);

    pressureLabel_ = lv_label_create(screen_);
    lv_obj_set_pos(pressureLabel_, 44, 399);
    lv_obj_set_style_text_font(pressureLabel_, &lv_font_montserrat_14, 0);
    lv_label_set_text(pressureLabel_, "WAITING");

    detailLabel_ = lv_label_create(screen_);
    lv_obj_set_pos(detailLabel_, 44, 421);
    lv_obj_set_style_text_font(detailLabel_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(detailLabel_, color_from_hex(0x6E8EA9), 0);
    lv_label_set_text(detailLabel_, "Waiting for Mac server");

    coverageLabel_ = lv_label_create(screen_);
    lv_obj_set_width(coverageLabel_, 92);
    lv_obj_set_pos(coverageLabel_, 258, 399);
    lv_obj_set_style_text_align(coverageLabel_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(coverageLabel_, &lv_font_montserrat_14, 0);
    lv_label_set_text(coverageLabel_, "0%");

    lv_obj_t* coverageCaption = lv_label_create(screen_);
    lv_obj_set_width(coverageCaption, 92);
    lv_obj_set_pos(coverageCaption, 258, 421);
    lv_obj_set_style_text_align(coverageCaption, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(coverageCaption, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(coverageCaption, color_from_hex(0x6E8EA9), 0);
    lv_label_set_text(coverageCaption, "coverage");

    resetButton_ = lv_btn_create(screen_);
    lv_obj_set_size(resetButton_, 72, 34);
    lv_obj_set_pos(resetButton_, 148, 400);
    lv_obj_set_style_radius(resetButton_, 17, 0);
    lv_obj_set_style_bg_color(resetButton_, color_from_hex(0x17344A), 0);
    lv_obj_set_style_border_width(resetButton_, 1, 0);
    lv_obj_set_style_border_color(resetButton_, color_from_hex(0x2A5974), 0);
    lv_obj_add_event_cb(resetButton_, resetEvent, LV_EVENT_CLICKED, this);
    resetButtonLabel_ = lv_label_create(resetButton_);
    lv_obj_center(resetButtonLabel_);
    lv_obj_set_style_text_font(resetButtonLabel_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(resetButtonLabel_, color_from_hex(0xB9D6E8), 0);
    lv_label_set_text(resetButtonLabel_, "RESET");

    bsp_display_unlock();
    ESP_LOGI(TAG, "Display UI ready");
    return true;
}

void BrushUi::makeCircle(size_t index, int x, int y, int size) {
    lv_obj_t* o = lv_obj_create(screen_);
    lv_obj_set_size(o, size, size);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_color(o, color_from_hex(0xEEF5FC), 0);
    lv_obj_set_style_bg_color(o, color_from_hex(0xD9EAFB), 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    surfaces_[index] = o;
}

void BrushUi::makeBar(size_t index, int x, int y, int w, int h) {
    lv_obj_t* o = lv_obj_create(screen_);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, 12, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_color(o, color_from_hex(0xEEF5FC), 0);
    lv_obj_set_style_bg_color(o, color_from_hex(0xD9EAFB), 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    surfaces_[index] = o;
}

void BrushUi::buildMouth() {
    // The goal here is structural similarity to the Oral-B coverage screen,
    // not final visual polish. These positions deliberately mirror the
    // browser simulator so later UI iterations remain easy to port.

    // Upper-left corner: outside / chewing / inside
    makeCircle(0, 25, 145, 50);
    makeCircle(1, 36, 179, 44);
    makeCircle(2, 48, 209, 38);

    // Upper-center: outside / inside
    makeBar(3, 99, 135, 170, 38);
    makeBar(4, 116, 181, 136, 30);

    // Upper-right corner: inside / chewing / outside
    makeCircle(5, 282, 209, 38);
    makeCircle(6, 288, 179, 44);
    makeCircle(7, 293, 145, 50);

    // Lower-left corner: outside / chewing / inside
    makeCircle(8, 25, 303, 50);
    makeCircle(9, 36, 274, 44);
    makeCircle(10, 48, 244, 38);

    // Lower-center: outside / inside
    makeBar(11, 99, 312, 170, 38);
    makeBar(12, 116, 274, 136, 30);

    // Lower-right corner: inside / chewing / outside
    makeCircle(13, 282, 244, 38);
    makeCircle(14, 288, 274, 44);
    makeCircle(15, 293, 303, 50);

    lv_obj_t* outside = lv_label_create(screen_);
    lv_obj_set_width(outside, 120);
    lv_obj_set_pos(outside, 124, 214);
    lv_obj_set_style_text_align(outside, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(outside, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(outside, color_from_hex(0x6E8EA9), 0);
    lv_label_set_text(outside, "Outside");

    lv_obj_t* chewing = lv_label_create(screen_);
    lv_obj_set_width(chewing, 120);
    lv_obj_set_pos(chewing, 124, 238);
    lv_obj_set_style_text_align(chewing, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(chewing, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(chewing, color_from_hex(0x6E8EA9), 0);
    lv_label_set_text(chewing, "Chewing");

    lv_obj_t* inside = lv_label_create(screen_);
    lv_obj_set_width(inside, 120);
    lv_obj_set_pos(inside, 124, 262);
    lv_obj_set_style_text_align(inside, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(inside, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(inside, color_from_hex(0x6E8EA9), 0);
    lv_label_set_text(inside, "Inside");
}

void BrushUi::styleSurface(lv_obj_t* obj, uint8_t coverage, bool active) {
    if (obj == nullptr) return;

    uint32_t fill = 0x123248;
    if (coverage >= 95) {
        fill = 0x1DBA9A;
    } else if (coverage > 0) {
        fill = 0x1681A4;
    }

    lv_obj_set_style_bg_color(obj, color_from_hex(fill), 0);

    if (active) {
        lv_obj_set_style_border_color(obj, color_from_hex(0xFFD166), 0);
        lv_obj_set_style_border_width(obj, 4, 0);
    } else {
        lv_obj_set_style_border_color(obj, color_from_hex(0x2A5974), 0);
        lv_obj_set_style_border_width(obj, 2, 0);
    }
}

void BrushUi::borderAnimation(void* object, int32_t value) {
    lv_obj_set_style_border_width(static_cast<lv_obj_t*>(object), value, 0);
}

void BrushUi::celebrationAnimation(void* object, int32_t value) {
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(object), static_cast<lv_opa_t>(value), 0);
}

void BrushUi::resetEvent(lv_event_t* event) {
    auto* ui = static_cast<BrushUi*>(lv_event_get_user_data(event));
    if (ui != nullptr) ui->resetRequested_ = true;
}

void BrushUi::pulseSurface(size_t index) {
    if (index >= surfaces_.size() || surfaces_[index] == nullptr) return;
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, surfaces_[index]);
    lv_anim_set_values(&animation, 2, 7);
    lv_anim_set_duration(&animation, 220);
    lv_anim_set_playback_duration(&animation, 220);
    lv_anim_set_repeat_count(&animation, 1);
    lv_anim_set_exec_cb(&animation, borderAnimation);
    lv_anim_start(&animation);
}

void BrushUi::showCelebration(const char* text) {
    if (celebrationLabel_ == nullptr) return;
    lv_label_set_text(celebrationLabel_, text);
    lv_obj_set_style_opa(celebrationLabel_, LV_OPA_COVER, 0);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, celebrationLabel_);
    lv_anim_set_values(&animation, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&animation, 1500);
    lv_anim_set_exec_cb(&animation, celebrationAnimation);
    lv_anim_start(&animation);
}

bool BrushUi::consumeResetRequest() {
    const bool requested = resetRequested_;
    resetRequested_ = false;
    return requested;
}

void BrushUi::resetVisualState() {
    previousCoverage_.fill(0);
    previousAllComplete_ = false;
    hasRendered_ = false;
}

void BrushUi::render(const BrushState& state) {
    if (screen_ == nullptr) {
        return;
    }

    if (!bsp_display_lock(1000)) {
        return;
    }

    char buf[48];

    std::snprintf(
        buf,
        sizeof(buf),
        "%02lu:%02lu",
        static_cast<unsigned long>(state.elapsedSeconds / 60),
        static_cast<unsigned long>(state.elapsedSeconds % 60)
    );
    lv_label_set_text(timer_, buf);
    lv_label_set_text(mode_, state.modeName);

    if (state.batteryPercent >= 0) {
        std::snprintf(buf, sizeof(buf), "%d%%", state.batteryPercent);
        lv_label_set_text(battery_, buf);
    } else {
        lv_label_set_text(battery_, "--");
    }

    uint32_t pressureColor = 0x9CA3AF;
    const char* pressureText = "WAITING";

    switch (state.pressure) {
        case Pressure::Low:
            pressureColor = 0x60A5FA;
            pressureText = "LOW PRESSURE";
            break;
        case Pressure::Normal:
            pressureColor = 0x4ADE80;
            pressureText = "GOOD PRESSURE";
            break;
        case Pressure::High:
            pressureColor = 0xF87171;
            pressureText = "HIGH PRESSURE";
            break;
        default:
            break;
    }

    lv_obj_set_style_bg_color(pressureDot_, color_from_hex(pressureColor), 0);
    lv_label_set_text(pressureLabel_, pressureText);

    if (!state.seen) {
        lv_label_set_text(detailLabel_, "Looking for toothbrush");
    } else if (!state.brushing) {
        lv_label_set_text(detailLabel_, "Brush idle");
    } else {
        std::snprintf(
            buf,
            sizeof(buf),
            "Pacer %u of %u",
            state.pacerSector,
            state.pacerSectorCount
        );
        lv_label_set_text(detailLabel_, buf);
    }

    uint32_t total = 0;
    bool allComplete = true;
    for (size_t i = 0; i < kMouthSurfaceCount; ++i) {
        if (state.coverage[i] < 95) allComplete = false;
        if (hasRendered_ && previousCoverage_[i] < 95 && state.coverage[i] >= 95) {
            pulseSurface(i);
        }
        total += state.coverage[i];
        styleSurface(
            surfaces_[i],
            state.coverage[i],
            state.activeSurface == static_cast<int8_t>(i)
        );
    }

    if (hasRendered_ && allComplete && !previousAllComplete_) {
        showCelebration("ALL SECTIONS COMPLETE");
    } else if (hasRendered_) {
        for (size_t i = 0; i < kMouthSurfaceCount; ++i) {
            if (previousCoverage_[i] < 95 && state.coverage[i] >= 95) {
                showCelebration("SECTION COMPLETE");
                break;
            }
        }
    }
    previousCoverage_ = state.coverage;
    previousAllComplete_ = allComplete;
    hasRendered_ = true;

    const uint32_t avg =
        total / static_cast<uint32_t>(kMouthSurfaceCount);

    std::snprintf(buf, sizeof(buf), "%lu%%", static_cast<unsigned long>(avg));
    lv_label_set_text(coverageLabel_, buf);

    bsp_display_unlock();
}
