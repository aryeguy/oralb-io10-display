idf_component_register(
    SRCS
        "app_main.cpp"
        "brush_model.cpp"
        "oralb_adv_parser.cpp"
        "ble_source.cpp"
        "mock_source.cpp"
        "mac_state_client.cpp"
        "ui.cpp"
        "qspi_display.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_client esp_driver_gpio json
             espressif__esp_lcd_touch espressif__esp_lvgl_port
)

# Persistent geometry belongs in static/heap storage, not a large task frame.
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror=frame-larger-than=2048)
