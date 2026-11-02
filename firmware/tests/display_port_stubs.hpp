#pragma once

// Host-only API doubles. Tests execute our initialization, not LCD hardware
// or the vendor driver's DMA implementation.
#include <cstdint>
using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int BSP_LCD_H_RES = 368;
constexpr int BSP_LCD_V_RES = 448;
constexpr int LV_COLOR_FORMAT_RGB565 = 1;
struct lv_display_t {};
struct lv_area_t { int32_t x1, y1, x2, y2; };
using esp_lcd_panel_handle_t = void*;
using esp_lcd_panel_io_handle_t = void*;
using esp_lcd_touch_handle_t = void*;
struct lvgl_port_cfg_t {};
#define ESP_LVGL_PORT_INIT_CONFIG() lvgl_port_cfg_t{}
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
struct bsp_display_config_t {};
struct lvgl_port_display_cfg_t {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    int buffer_size, hres, vres, color_format;
    struct { bool buff_dma, swap_bytes; } flags;
    void (*rounder_cb)(lv_area_t*);
};
struct lvgl_port_touch_cfg_t {
    lv_display_t* disp;
    esp_lcd_touch_handle_t handle;
};
esp_err_t lvgl_port_init(const lvgl_port_cfg_t*);
esp_err_t bsp_display_new(const bsp_display_config_t*, esp_lcd_panel_handle_t*, esp_lcd_panel_io_handle_t*);
esp_err_t bsp_touch_new(const void*, esp_lcd_touch_handle_t*);
lv_display_t* lvgl_port_add_disp(const lvgl_port_display_cfg_t*);
void* lvgl_port_add_touch(const lvgl_port_touch_cfg_t*);
