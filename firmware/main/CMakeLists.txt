idf_component_register(
    SRCS
        "app_main.cpp"
        "brush_model.cpp"
        "oralb_adv_parser.cpp"
        "ble_source.cpp"
        "mock_source.cpp"
        "mac_state_client.cpp"
        "ui.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_client json
)
