#include "qspi_display.hpp"
#include "display_port_stubs.hpp"
#include <cassert>

static lv_display_t display;
static int panelToken, ioToken, touchToken;
static bool failPort, failPanel, failTouch, failDisplay;
static int stage, displayCalls, touchCalls;

esp_err_t lvgl_port_init(const lvgl_port_cfg_t*) {
    assert(stage++ == 0);
    return failPort ? -1 : ESP_OK;
}
esp_err_t bsp_display_new(const bsp_display_config_t*, void** panel, void** io) {
    assert(stage++ == 1);
    *panel = &panelToken;
    *io = &ioToken;
    return failPanel ? -1 : ESP_OK;
}
esp_err_t bsp_touch_new(const void*, void** touch) {
    assert(stage++ == 2); // Board offset detection precedes registration.
    *touch = &touchToken;
    return failTouch ? -1 : ESP_OK;
}
lv_display_t* lvgl_port_add_disp(const lvgl_port_display_cfg_t* config) {
    assert(stage++ == 3);
    ++displayCalls;
    assert(config->panel_handle == &panelToken);
    assert(config->io_handle == &ioToken);
    assert(config->hres == BSP_LCD_H_RES && config->vres == BSP_LCD_V_RES);
    assert(config->buffer_size == BSP_LCD_H_RES * 20);
    assert(config->color_format == LV_COLOR_FORMAT_RGB565);
    assert(config->flags.buff_dma && config->flags.swap_bytes);
    assert(config->rounder_cb);
    lv_area_t odd{1, 3, 16, 28};
    config->rounder_cb(&odd);
    assert(odd.x1 == 0 && odd.y1 == 2 && odd.x2 == 17 && odd.y2 == 29);
    lv_area_t full{0, 0, BSP_LCD_H_RES - 1, BSP_LCD_V_RES - 1};
    config->rounder_cb(&full);
    assert(full.x2 == BSP_LCD_H_RES - 1 && full.y2 == BSP_LCD_V_RES - 1);
    return failDisplay ? nullptr : &display;
}
void* lvgl_port_add_touch(const lvgl_port_touch_cfg_t* config) {
    assert(stage++ == 4);
    ++touchCalls;
    assert(config->disp == &display && config->handle == &touchToken);
    return &touchToken;
}
static void reset() {
    stage = displayCalls = touchCalls = 0;
    failPort = failPanel = failTouch = failDisplay = false;
}
int main() {
    reset();
    assert(start_qspi_display() == &display);
    assert(displayCalls == 1 && touchCalls == 1);
    reset(); failTouch = true;
    assert(start_qspi_display() == &display);
    assert(displayCalls == 1 && touchCalls == 0);
    reset(); failPort = true;
    assert(start_qspi_display() == nullptr && stage == 1);
    reset(); failPanel = true;
    assert(start_qspi_display() == nullptr && stage == 2);
    reset(); failDisplay = true;
    assert(start_qspi_display() == nullptr && touchCalls == 0);
}
