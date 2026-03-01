idf_component_register(
    SRCS "main.cpp"
         "usb_host_manager.cpp"
         "hid_device_manager.cpp"
         "hid_parser.cpp"
         "input_decoder.cpp"
         "ble_gamepad.cpp"
         "debug_portal.cpp"
    INCLUDE_DIRS "."
    REQUIRES usb usb_host_hid esp_timer esp_hid nvs_flash bt freertos esp_wifi esp_http_server log esp_driver_gpio
)

set_source_files_properties(ble_gamepad.cpp PROPERTIES COMPILE_FLAGS "-Wno-missing-field-initializers")

