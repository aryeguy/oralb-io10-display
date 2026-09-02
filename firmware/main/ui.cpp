#include "ui.hpp"

#include "app_config.hpp"
#include "bsp/esp-bsp.h"

#include <algorithm>
#include <cstdio>

static lv_color_t color_from_hex(uint32_t hex) {
    return lv_color_hex(hex);
}

bool BrushUi::begin() {
    lv_display_t* display = bsp_display_start();
    if (display == nullptr) {
        return false;
    }

    bsp_display_brightness_set(app_config::kDisplayBrightnessPercent);

    if (!bsp_display_lock(0)) {
        return false;
    }

    screen_ = lv_screen_active();
    lv_obj_set_style_bg_color(screen_, color_from_hex(0xEEF4FB), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen_, color_from_hex(0x10294B), 0);

    // Mode
    mode_ = lv_label_create(screen_);
    lv_obj_set_pos(mode_, 18, 17);
    lv_obj_set_style_text_font(mode_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(mode_, color_from_hex(0x607892), 0);
    lv_label_set_text(mode_, "DAILY CLEAN");

    // Battery
    battery_ = lv_label_create(screen_);
    lv_obj_set_pos(battery_, 315, 17);
    lv_obj_set_style_text_font(battery_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(battery_, color_from_hex(0x607892), 0);
    lv_label_set_text(battery_, "--");

    // Timer
    timer_ = lv_label_create(screen_);
    lv_obj_set_width(timer_, 368);
    lv_obj_set_pos(timer_, 0, 38);
    lv_obj_set_style_text_align(timer_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(timer_, &lv_font_montserrat_48, 0);
    lv_label_set_text(timer_, "00:00");

    lv_obj_t* goal = lv_label_create(screen_);
    lv_obj_set_width(goal, 368);
    lv_obj_set_pos(goal, 0, 91);
    lv_obj_set_style_text_align(goal, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(goal, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(goal, color_from_hex(0x9CAFC2), 0);
    lv_label_set_text(goal, "2:00 goal");

    buildMouth();

    // Bottom status
    pressureDot_ = lv_obj_create(screen_);
    lv_obj_set_size(pressureDot_, 16, 16);
    lv_obj_set_pos(pressureDot_, 18, 388);
    lv_obj_set_style_radius(pressureDot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(pressureDot_, 0, 0);
    lv_obj_set_style_bg_color(pressureDot_, color_from_hex(0x4ADE80), 0);

    pressureLabel_ = lv_label_create(screen_);
    lv_obj_set_pos(pressureLabel_, 43, 382);
    lv_obj_set_style_text_font(pressureLabel_, &lv_font_montserrat_14, 0);
    lv_label_set_text(pressureLabel_, "WAITING");

    detailLabel_ = lv_label_create(screen_);
    lv_obj_set_pos(detailLabel_, 43, 402);
    lv_obj_set_style_text_font(detailLabel_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(detailLabel_, color_from_hex(0x95A8BC), 0);
    lv_label_set_text(detailLabel_, "Looking for toothbrush");

    coverageLabel_ = lv_label_create(screen_);
    lv_obj_set_width(coverageLabel_, 75);
    lv_obj_set_pos(coverageLabel_, 278, 383);
    lv_obj_set_style_text_align(coverageLabel_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(coverageLabel_, &lv_font_montserrat_14, 0);
    lv_label_set_text(coverageLabel_, "0%");

    lv_obj_t* coverageCaption = lv_label_create(screen_);
    lv_obj_set_width(coverageCaption, 75);
    lv_obj_set_pos(coverageCaption, 278, 402);
    lv_obj_set_style_text_align(coverageCaption, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(coverageCaption, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(coverageCaption, color_from_hex(0x95A8BC), 0);
    lv_label_set_text(coverageCaption, "coverage");

    bsp_display_unlock();
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
    makeCircle(0, 48, 142, 34);
    makeCircle(1, 57, 165, 32);
    makeCircle(2, 66, 187, 30);

    // Upper-center: outside / inside
    makeBar(3, 109, 132, 150, 30);
    makeBar(4, 127, 166, 114, 22);

    // Upper-right corner: inside / chewing / outside
    makeCircle(5, 272, 187, 30);
    makeCircle(6, 279, 165, 32);
    makeCircle(7, 286, 142, 34);

    // Lower-left corner: outside / chewing / inside
    makeCircle(8, 48, 296, 34);
    makeCircle(9, 57, 273, 32);
    makeCircle(10, 66, 251, 30);

    // Lower-center: outside / inside
    makeBar(11, 109, 316, 150, 30);
    makeBar(12, 127, 286, 114, 22);

    // Lower-right corner: inside / chewing / outside
    makeCircle(13, 272, 251, 30);
    makeCircle(14, 279, 273, 32);
    makeCircle(15, 286, 296, 34);

    lv_obj_t* outside = lv_label_create(screen_);
    lv_obj_set_width(outside, 120);
    lv_obj_set_pos(outside, 124, 212);
    lv_obj_set_style_text_align(outside, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(outside, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(outside, color_from_hex(0x70849A), 0);
    lv_label_set_text(outside, "Outside");

    lv_obj_t* chewing = lv_label_create(screen_);
    lv_obj_set_width(120);
    lv_obj_set_pos(chewing, 124, 235);
    lv_obj_set_style_text_align(chewing, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(chewing, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(chewing, color_from_hex(0x70849A), 0);
    lv_label_set_text(chewing, "Chewing");

    lv_obj_t* inside = lv_label_create(screen_);
    lv_obj_set_width(120);
    lv_obj_set_pos(inside, 124, 258);
    lv_obj_set_style_text_align(inside, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(inside, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(inside, color_from_hex(0x70849A), 0);
    lv_label_set_text(inside, "Inside");
}

void BrushUi::styleSurface(lv_obj_t* obj, uint8_t coverage, bool active) {
    if (obj == nullptr) return;

    uint32_t fill = 0xD9EAFB;
    if (coverage >= 95) {
        fill = 0x4F9CE8;
    } else if (coverage > 0) {
        fill = 0x9FCAF2;
    }

    lv_obj_set_style_bg_color(obj, color_from_hex(fill), 0);

    if (active) {
        lv_obj_set_style_border_color(obj, color_from_hex(0x176FD0), 0);
        lv_obj_set_style_border_width(obj, 4, 0);
    } else {
        lv_obj_set_style_border_color(obj, color_from_hex(0xEEF5FC), 0);
        lv_obj_set_style_border_width(obj, 2, 0);
    }
}

void BrushUi::render(const BrushState& state) {
    if (screen_ == nullptr) {
        return;
    }

    if (!bsp_display_lock(0)) {
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
    for (size_t i = 0; i < kMouthSurfaceCount; ++i) {
        total += state.coverage[i];
        styleSurface(
            surfaces_[i],
            state.coverage[i],
            state.activeSurface == static_cast<int8_t>(i)
        );
    }

    const uint32_t avg =
        total / static_cast<uint32_t>(kMouthSurfaceCount);

    std::snprintf(buf, sizeof(buf), "%lu%%", static_cast<unsigned long>(avg));
    lv_label_set_text(coverageLabel_, buf);

    bsp_display_unlock();
}
