idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES LovyanGFX esp_driver_usb_serial_jtag esp_system
)