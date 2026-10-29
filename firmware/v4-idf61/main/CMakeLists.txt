idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        nn2_net nn2_wifi nn2_config nn2_app nn2_ports esp_timer esp_hw_support
        esp_driver_usb_serial_jtag
        freertos
        log
        nocti_v4_common
        nocti_v4_board
        nocti_v4_i2c
        nocti_v4_led
        nocti_v4_health
        nocti_v4_time
        nocti_v4_ads
        nocti_v4_async_led
        nocti_v4_scheduler
        nocti_v4_optical
        nocti_v4_network
)
