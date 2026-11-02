#include "qspi_display.hpp"

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_lcd_touch.h"
#include "bsp/touch.h"
#include "esp_lvgl_port.h"
#include "esp_lvgl_port_touch.h"
#include "esp_log.h"

lv_display_t* start_qspi_display() {
    const lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    if (lvgl_port_init(&port) != ESP_OK) return nullptr;

    esp_lcd_panel_handle_t panel = nullptr;
    esp_lcd_panel_io_handle_t io = nullptr;
    bsp_display_config_t hardware{};
    if (bsp_display_new(&hardware, &panel, &io) != ESP_OK) return nullptr;

    // Touch detection also selects the board revision's LCD column offset.
    esp_lcd_touch_handle_t touch = nullptr;
    const esp_err_t touchResult = bsp_touch_new(nullptr, &touch);

    lvgl_port_display_cfg_t config{};
    config.io_handle = io;
    config.panel_handle = panel;
    config.buffer_size = BSP_LCD_H_RES * 20;
    config.hres = BSP_LCD_H_RES;
    config.vres = BSP_LCD_V_RES;
    config.color_format = LV_COLOR_FORMAT_RGB565;
    config.flags.buff_dma = true;
    config.flags.swap_bytes = true;
    config.rounder_cb = [](lv_area_t* area) {
        area->x1 &= ~1;
        area->y1 &= ~1;
        area->x2 |= 1;
        area->y2 |= 1;
    };

    // CO5300 uses QSPI, not an RGB framebuffer. The SPI port registers
    // on_color_trans_done and holds the draw buffer until DMA completes.
    // bsp_display_start() in the bundled BSP incorrectly selects add_disp_rgb.
    lv_display_t* display = lvgl_port_add_disp(&config);
    if (!display) return nullptr;

    if (touchResult == ESP_OK) {
        lvgl_port_touch_cfg_t touchConfig{};
        touchConfig.disp = display;
        touchConfig.handle = touch;
        if (!lvgl_port_add_touch(&touchConfig)) {
            ESP_LOGW("qspi_display", "Touch registration failed");
        }
    } else {
        ESP_LOGW("qspi_display", "Touch initialization failed: %s", esp_err_to_name(touchResult));
    }
    ESP_LOGI("qspi_display", "QSPI display ready with DMA completion synchronization");
    return display;
}
